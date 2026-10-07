/* vfs.c - virtual filesystem shim. See vfs.h for the design overview.
 *
 * Backends:
 *   fat_read_ops  - read-only view of a FAT16 file (delegates to fat_f*).
 *   mem_file_ops  - write-mode FAT file: writes accumulate in a heap
 *                   buffer and are flushed with fat_write() on close
 *                   (fat.c has no streaming-write API).
 *   *_dev ops     - /dev/null, /dev/zero, /dev/serial, /dev/kbd.
 *   pipe_ops      - one 4 KB ring shared by a read end and a write end,
 *                   with blocking semantics on both sides (vfs_pipe).
 */

#include "vfs.h"
#include "fat.h"
#include "heap.h"
#include "str.h"
#include "keyboard.h"
#include "serial.h"
#include "task.h"
#include "proc.h"

/* ---- open-handle table ---- */
static vfs_node_t nodes[VFS_MAX_OPEN];

static vfs_node_t *alloc_node(void) {
    for (int i = 0; i < VFS_MAX_OPEN; i++) {
        if (!nodes[i].used) {
            memset(&nodes[i], 0, sizeof(nodes[i]));
            nodes[i].used       = 1;
            nodes[i].backend_fd = -1;
            return &nodes[i];
        }
    }
    return 0;   /* table full */
}

static void copy_name(char *dst, const char *src) {
    uint32_t i = 0;
    while (src[i] && i < VFS_NAME_MAX - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}


/* ---- pipe backend --------------------------------------------------------
 *
 * One ring buffer shared by two handles. The interesting part is not the ring,
 * it is when each side gives up:
 *
 *   - a reader blocks while the pipe is empty AND a writer still exists; once
 *     the last writer closes, an empty pipe reads 0, which is EOF;
 *   - a writer blocks while the pipe is full AND a reader still exists; once
 *     the last reader closes, writing fails rather than blocking forever.
 *
 * Both also give up when the running process has a kill pending, so a program
 * parked on a pipe nobody will ever write to stays killable. Without that,
 * `cook cat.elf` with no input would be an unkillable process.
 */
#define PIPE_CAP  4096

typedef struct {
    uint8_t      data[PIPE_CAP];
    uint32_t     head, tail, count;
    int          readers, writers;
    wait_queue_t rwq, wwq;
} pipe_t;

/* Block with interrupts enabled, then put the caller's IF back.
 *
 * Pipe I/O is reached from syscall_dispatch, which arrives through an
 * interrupt gate with IF clear, and task_block_on ends in hlt when nothing
 * else is runnable: blocking with interrupts off would never wake. Same
 * requirement proc_take_stop has. */
static void pipe_block(wait_queue_t *wq) {
    uint32_t fl;
    __asm__ volatile ("pushf; pop %0" : "=r"(fl));
    __asm__ volatile ("sti");
    task_block_on(wq);
    if (!(fl & 0x200)) __asm__ volatile ("cli");
}

static int pipe_read_op(vfs_node_t *n, void *buf, uint32_t count) {
    pipe_t *p = (pipe_t *)n->priv;
    uint8_t *out = (uint8_t *)buf;
    if (!p || count == 0) return 0;

    for (;;) {
        preempt_disable();
        if (p->count > 0 || p->writers == 0) { preempt_enable(); break; }
        if (proc_kill_pending())             { preempt_enable(); return 0; }
        pipe_block(&p->rwq);
        preempt_enable();
    }

    uint32_t got = 0;
    while (got < count && p->count > 0) {
        out[got++] = p->data[p->tail];
        p->tail = (p->tail + 1) % PIPE_CAP;
        p->count--;
    }
    n->pos += got;
    if (got) task_wake(&p->wwq);
    return (int)got;              /* 0 = every writer closed and we drained it */
}

static int pipe_write_op(vfs_node_t *n, const void *buf, uint32_t count) {
    pipe_t *p = (pipe_t *)n->priv;
    const uint8_t *in = (const uint8_t *)buf;
    if (!p) return -1;

    uint32_t put = 0;
    while (put < count) {
        for (;;) {
            preempt_disable();
            if (p->count < PIPE_CAP || p->readers == 0) { preempt_enable(); break; }
            if (proc_kill_pending())                    { preempt_enable(); return (int)put; }
            pipe_block(&p->wwq);
            preempt_enable();
        }
        if (p->readers == 0) return put ? (int)put : -1;   /* nobody left to read */

        while (put < count && p->count < PIPE_CAP) {
            p->data[p->head] = in[put++];
            p->head = (p->head + 1) % PIPE_CAP;
            p->count++;
        }
        task_wake(&p->rwq);
    }
    n->pos += put;
    return (int)put;
}

/* Closing one end wakes the other, so a peer blocked on a pipe that just lost
 * its last writer (or reader) re-tests and leaves. The buffer goes when both
 * ends are gone. */
static void pipe_close_op(vfs_node_t *n) {
    pipe_t *p = (pipe_t *)n->priv;
    if (!p) return;
    if (n->flags & VFS_WRONLY) p->writers--; else p->readers--;
    task_wake(&p->rwq);
    task_wake(&p->wwq);
    n->priv = 0;
    if (p->readers <= 0 && p->writers <= 0) kfree(p);
}

static const vfs_ops_t pipe_ops = { pipe_read_op, pipe_write_op, 0, pipe_close_op };

int vfs_pipe(vfs_node_t **rd, vfs_node_t **wr) {
    if (!rd || !wr) return -1;
    vfs_node_t *r = alloc_node();
    if (!r) return -1;
    vfs_node_t *w = alloc_node();
    if (!w) { r->used = 0; return -1; }

    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (!p) { r->used = 0; w->used = 0; return -1; }
    memset(p, 0, sizeof(*p));
    p->readers = 1;
    p->writers = 1;

    copy_name(r->name, "pipe:r");  r->type = VFS_CHARDEV;
    r->flags = VFS_RDONLY; r->ops = &pipe_ops; r->priv = p;
    copy_name(w->name, "pipe:w");  w->type = VFS_CHARDEV;
    w->flags = VFS_WRONLY; w->ops = &pipe_ops; w->priv = p;

    *rd = r; *wr = w;
    return 0;
}

/* ---- FAT read-only backend ---- */
static int fat_read_op(vfs_node_t *n, void *buf, uint32_t count) {
    int r = fat_fread(n->backend_fd, buf, count);
    if (r > 0) n->pos += (uint32_t)r;
    return r;
}
static int fat_seek_op(vfs_node_t *n, int32_t off, int whence) {
    if (fat_fseek(n->backend_fd, off, whence) < 0) return -1;
    n->pos = (uint32_t)fat_ftell(n->backend_fd);
    return 0;
}
static void fat_close_op(vfs_node_t *n) {
    fat_fclose(n->backend_fd);
}
static const vfs_ops_t fat_read_ops = {
    fat_read_op, 0, fat_seek_op, fat_close_op
};

/* ---- FAT write backend (buffer in heap, flush on close) ---- */
static int mem_ensure(vfs_node_t *n, uint32_t need) {
    if (need <= n->cap) return 0;
    uint32_t nc = n->cap ? n->cap : 512;
    while (nc < need) nc <<= 1;
    uint8_t *nb = n->buf ? (uint8_t *)krealloc(n->buf, nc)
                         : (uint8_t *)kmalloc(nc);
    if (!nb) return -1;
    n->buf = nb;
    n->cap = nc;
    return 0;
}
static int mem_write_op(vfs_node_t *n, const void *buf, uint32_t count) {
    if (mem_ensure(n, n->pos + count) < 0) return -1;
    memcpy(n->buf + n->pos, buf, count);
    n->pos += count;
    if (n->pos > n->size) n->size = n->pos;
    return (int)count;
}
static int mem_read_op(vfs_node_t *n, void *buf, uint32_t count) {
    if (n->pos >= n->size) return 0;
    uint32_t avail = n->size - n->pos;
    if (count > avail) count = avail;
    if (n->buf) memcpy(buf, n->buf + n->pos, count);
    n->pos += count;
    return (int)count;
}
static int mem_seek_op(vfs_node_t *n, int32_t off, int whence) {
    int32_t np;
    if      (whence == VFS_SEEK_SET) np = off;
    else if (whence == VFS_SEEK_CUR) np = (int32_t)n->pos + off;
    else if (whence == VFS_SEEK_END) np = (int32_t)n->size + off;
    else return -1;
    if (np < 0) return -1;
    n->pos = (uint32_t)np;
    return 0;
}
static void mem_close_op(vfs_node_t *n) {
    static const uint8_t empty = 0;
    /* Flush the accumulated buffer to disk as a whole-file write. */
    fat_write(n->name, n->buf ? n->buf : &empty, n->size);
    if (n->buf) kfree(n->buf);
}
static const vfs_ops_t mem_file_ops = {
    mem_read_op, mem_write_op, mem_seek_op, mem_close_op
};

/* ---- character devices ---- */
static int dev_null_read(vfs_node_t *n, void *buf, uint32_t count) {
    (void)n; (void)buf; (void)count;
    return 0;                         /* always at EOF */
}
static int dev_sink_write(vfs_node_t *n, const void *buf, uint32_t count) {
    (void)buf;
    n->pos += count;
    return (int)count;                /* swallow everything */
}
static int dev_zero_read(vfs_node_t *n, void *buf, uint32_t count) {
    memset(buf, 0, count);
    n->pos += count;
    return (int)count;
}
static int dev_serial_write(vfs_node_t *n, const void *buf, uint32_t count) {
    const uint8_t *p = (const uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) serial_putc((char)p[i]);
    n->pos += count;
    return (int)count;
}
/* Line-oriented blocking read: returns at `count` bytes or after '\n'. */
static int dev_kbd_read(vfs_node_t *n, void *buf, uint32_t count) {
    uint8_t *b = (uint8_t *)buf;
    uint32_t i = 0;
    while (i < count) {
        int c = keyboard_getchar();
        if (c < 0)    break;
        if (c > 0xFF) continue;       /* skip arrows / function keys */
        b[i++] = (uint8_t)c;
        n->pos++;
        if (c == '\n') break;
    }
    return (int)i;
}

static const vfs_ops_t null_ops   = { dev_null_read, dev_sink_write,   0, 0 };
static const vfs_ops_t zero_ops   = { dev_zero_read, dev_sink_write,   0, 0 };
static const vfs_ops_t serial_ops = { 0,             dev_serial_write, 0, 0 };
static const vfs_ops_t kbd_ops    = { dev_kbd_read,  0,                0, 0 };

/* devices[] also drives vfs_ls(). */
static const struct { const char *path; const vfs_ops_t *ops; } devices[] = {
    { "dev/null",   &null_ops   },
    { "dev/zero",   &zero_ops   },
    { "dev/serial", &serial_ops },
    { "dev/kbd",    &kbd_ops    },
};
#define N_DEVICES ((int)(sizeof(devices) / sizeof(devices[0])))

/* ---- public API ---- */
void vfs_init(void) {
    memset(nodes, 0, sizeof(nodes));
}

vfs_node_t *vfs_open(const char *path, int flags) {
    if (!path) return 0;
    const char *p = path;
    while (*p == '/') p++;            /* tolerate a leading slash */
    if (!p[0]) return 0;

    /* device node? */
    for (int i = 0; i < N_DEVICES; i++) {
        if (strcmp(p, devices[i].path) == 0) {
            vfs_node_t *n = alloc_node();
            if (!n) return 0;
            n->type  = VFS_CHARDEV;
            n->flags = flags;
            n->ops   = devices[i].ops;
            copy_name(n->name, p);
            return n;
        }
    }

    /* regular FAT file */
    int write = (flags & VFS_WRONLY) != 0;
    vfs_node_t *n = alloc_node();
    if (!n) return 0;
    n->type  = VFS_FILE;
    n->flags = flags;
    copy_name(n->name, p);

    if (write) {
        /* Write mode: buffer is created on first write; an empty file is
         * still produced if the handle is closed with nothing written. */
        n->ops = &mem_file_ops;
        return n;
    }

    int fd = fat_fopen(p);
    if (fd < 0) { n->used = 0; return 0; }
    n->backend_fd = fd;
    n->size       = fat_fsize(fd);
    n->ops        = &fat_read_ops;
    return n;
}

int vfs_read(vfs_node_t *n, void *buf, uint32_t count) {
    if (!n || !n->used || !buf)          return -1;
    if (!(n->flags & VFS_RDONLY))        return -1;   /* not opened for read */
    if (!n->ops || !n->ops->read)        return -1;
    if (count == 0)                      return 0;
    return n->ops->read(n, buf, count);
}

int vfs_write(vfs_node_t *n, const void *buf, uint32_t count) {
    if (!n || !n->used || !buf)          return -1;
    if (!(n->flags & VFS_WRONLY))        return -1;   /* not opened for write */
    if (!n->ops || !n->ops->write)       return -1;
    if (count == 0)                      return 0;
    return n->ops->write(n, buf, count);
}

int vfs_seek(vfs_node_t *n, int32_t off, int whence) {
    if (!n || !n->used || !n->ops || !n->ops->seek) return -1;
    return n->ops->seek(n, off, whence);
}

int32_t vfs_tell(vfs_node_t *n) {
    return (n && n->used) ? (int32_t)n->pos : -1;
}

uint32_t vfs_size(vfs_node_t *n) {
    return (n && n->used) ? n->size : 0;
}

int vfs_eof(vfs_node_t *n) {
    if (!n || !n->used)            return 1;
    if (n->type == VFS_CHARDEV)    return 0;   /* streams never hit EOF */
    return n->pos >= n->size;
}

void vfs_close(vfs_node_t *n) {
    if (!n || !n->used) return;
    if (n->ops && n->ops->close) n->ops->close(n);
    n->used = 0;
}

int vfs_ls(const char *path, vfs_dirent_t *out, int max) {
    if (!out || max <= 0) return -1;
    const char *p = path ? path : "";
    while (*p == '/') p++;
    int is_root = (p[0] == '\0');

    int n = 0;

    /* /dev pseudo-directory is merged into the root listing only */
    if (is_root) {
        for (int i = 0; i < N_DEVICES && n < max; i++) {
            copy_name(out[n].name, devices[i].path);
            out[n].type = VFS_CHARDEV;
            out[n].size = 0;
            n++;
        }
    }

    /* FAT entries for the requested directory ("bowl") */
    fat_entry_t fe[FAT_LS_MAX];
    int fc = fat_ls(is_root ? "/" : path, fe, FAT_LS_MAX);
    if (fc < 0) return is_root ? n : -1;
    for (int i = 0; i < fc && n < max; i++) {
        if (fe[i].attr & FAT_ATTR_VOLID) continue;   /* skip volume label */
        copy_name(out[n].name, fe[i].name);
        out[n].type = VFS_FILE;
        out[n].size = fe[i].size;
        n++;
    }
    return n;
}

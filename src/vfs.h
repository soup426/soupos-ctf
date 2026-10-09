#pragma once
#include <stdint.h>

/* vfs.h - thin virtual filesystem shim for soupOS.
 *
 * The VFS gives every consumer one uniform open/read/write/seek/close API
 * regardless of what is behind a path:
 *
 *   - regular files          -> the FAT16 backend (fat.c)
 *   - /dev/null, /dev/zero   -> in-kernel pseudo devices
 *   - /dev/serial            -> COM1 UART (write-only sink)
 *   - /dev/kbd               -> PS/2 keyboard (read-only stream)
 *
 * Each backend supplies a vfs_ops_t table; vfs_open() picks the backend by
 * path and the rest of the API just dispatches through it. fat_* keeps
 * working untouched - the VFS is an additive layer, not a rewrite.
 */

/* Open flags (access mode in the low two bits). */
#define VFS_RDONLY  0x01
#define VFS_WRONLY  0x02
#define VFS_RDWR    0x03
#define VFS_CREATE  0x04        /* create/truncate a FAT file on open   */
#define VFS_APPEND  0x08        /* write after what the file holds (>>, v0.56.0) */

/* seek whence - values match FAT_SEEK_* on purpose. */
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

#define VFS_MAX_OPEN  16
#define VFS_NAME_MAX  32

typedef enum {
    VFS_FILE    = 0,            /* seekable, sized                      */
    VFS_CHARDEV = 1,            /* byte stream, no meaningful size      */
} vfs_type_t;

struct vfs_node;

/* Backend operation table. Any entry may be NULL if unsupported. */
typedef struct vfs_ops {
    int  (*read) (struct vfs_node *n, void *buf, uint32_t count);
    int  (*write)(struct vfs_node *n, const void *buf, uint32_t count);
    int  (*seek) (struct vfs_node *n, int32_t off, int whence);
    void (*close)(struct vfs_node *n);
} vfs_ops_t;

/* An open handle. node->pos and node->size are authoritative for tell/
 * size/eof; backends keep them in sync inside their read/seek ops. */
typedef struct vfs_node {
    char             name[VFS_NAME_MAX];
    vfs_type_t       type;
    int              flags;
    uint32_t         pos;       /* current byte offset                  */
    uint32_t         size;      /* file size (0 for char devices)       */
    const vfs_ops_t *ops;
    int              backend_fd;/* FAT fd for read-mode files, else -1  */
    uint8_t        **pg;        /* write mode: the file's bytes, in 4 KB
                                 * physical pages (v0.56.4; was one block
                                 * of the 8 MB kernel heap, which stopped
                                 * growing at about 2 MB, silently)     */
    uint32_t         npg, pgcap;/* pages held, and room in pg[]          */
    int              failed;    /* a write or the flush failed           */
    void            *priv;      /* backend-private state (pipes)        */
    int              used;
} vfs_node_t;

/* Directory entry returned by vfs_ls(). */
typedef struct {
    char       name[VFS_NAME_MAX];
    vfs_type_t type;
    uint32_t   size;
} vfs_dirent_t;

/* Clear the open-handle table. Call once at boot after fat_init(). */
void vfs_init(void);

/* Open a path. Returns a handle or NULL on error. */
vfs_node_t *vfs_open(const char *path, int flags);

/* Stream I/O. read/write return bytes transferred, or -1 on error.
 * read returns 0 at end-of-file. */
int      vfs_read (vfs_node_t *n, void *buf, uint32_t count);
int      vfs_write(vfs_node_t *n, const void *buf, uint32_t count);
int      vfs_seek (vfs_node_t *n, int32_t off, int whence);
int32_t  vfs_tell (vfs_node_t *n);
uint32_t vfs_size (vfs_node_t *n);
int      vfs_eof  (vfs_node_t *n);

/* Flush (write-mode files) and release the handle. */
/* 0, or -1 if anything written through this handle did not reach the
 * disk whole (v0.56.4). */
int      vfs_close(vfs_node_t *n);

/* Create a pipe: whatever is written to *wr can be read from *rd. Both are
 * ordinary handles and are closed with vfs_close like anything else; the
 * shared buffer goes away when both ends are closed.
 *
 * read blocks while the pipe is empty and a writer still exists, and returns 0
 * (EOF) once the buffer is empty and the last writer has closed. write blocks
 * while the pipe is full and a reader still exists, and returns -1 once every
 * reader has closed. Returns 0, or -1 if the handle table or memory is out. */
int      vfs_pipe(vfs_node_t **rd, vfs_node_t **wr);

/* List a directory ("bowl"). The root listing merges the FAT root with the
 * /dev nodes; subdirectories list their FAT contents. Returns entry count,
 * or -1 on error. */
int      vfs_ls(const char *path, vfs_dirent_t *out, int max);

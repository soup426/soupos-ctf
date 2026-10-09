/* usermode.c - the ring-3 / syscall / ELF-loader side of soupOS.
 *
 * A loaded program lives in high virtual space (USER_BASE and up), well above
 * the identity-mapped low RAM, backed by fresh user-accessible pages in a page
 * directory of its own. Isolation runs both ways: the U/S bit keeps ring 3 out
 * of the kernel, and the private directory keeps one program out of another's
 * pages.
 *
 * Per-run state lives on the proc_t (proc.c), not in file-scope variables
 * here. That it used to live here is exactly what limited the machine to one
 * program at a time: a second program would have overwritten the first one's
 * fd table, heap break, args and exit code.
 */
#include "random.h"
#include "usermode.h"
#include "term.h"
#include "swap.h"
#include "syscall_nr.h"
#include "isr.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "fat.h"
#include "users.h"
#include "str.h"
#include "vga.h"
#include "serial.h"
#include "keyboard.h"
#include "gdt.h"
#include "klog.h"
#include "vfs.h"
#include "task.h"
#include "timer.h"
#include "proc.h"
#include "countertop.h"

/* ---- user address-space layout ----
 *   0xC0000000  program image (PT_LOAD)
 *   0xC4000000  heap (grows up, via sbrk)
 *   0xC8000000  top of stack (grows down)
 * All above the identity-mapped low RAM, so there is no aliasing. */
#define USER_LO       0xC0000000u
#define USER_HI       0xC8000000u
#define USER_HEAP     0xC4000000u
#define USTACK_TOP    0xC8000000u    /* top of the user stack            */
#define USTACK_PAGES  4              /* mapped at exec; the rest on demand */
#define USTACK_MAX    (1024u * 1024u) /* the stack may grow to 1 MB...  */
#define USTACK_GUARD  (USTACK_TOP - USTACK_MAX)  /* ...and this page is never mapped */
#define MAX_ELF       (256 * 1024)   /* largest program we'll load       */

/* ---- ELF32 ---- */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} __attribute__((packed)) elf32_phdr_t;

#define PT_LOAD   1
#define ET_EXEC   2
#define EM_386    3

/* Details of the most recent ring-3 fault, for the shell to report. Kept here
 * rather than on the process on purpose: it is a diagnostic about the machine,
 * read right after the event, and with several processes alive the last writer
 * simply wins. The *outcome* of a fault is not here, it is the process's exit
 * code. */
static uint32_t g_fault_exc, g_fault_err, g_fault_eip, g_fault_cr2;

/* A user pointer is acceptable only if it lies entirely within user space, so
 * a buggy/hostile program cannot make the kernel read or write elsewhere.
 *
 * And only if the program has every page of it (v0.60.149): mapped already,
 * or one the demand pager would give it on first touch (its heap below the
 * break, its stack above the guard page). The range alone let a program
 * hand SYS_WRITE 0xC6000000, above its break: the kernel's own copy faulted,
 * the pager declined, and isr.c panicked the machine. Checked before the
 * copy rather than caught in the fault, because by then the call may hold
 * a lock (FAT's, the desktop's) that unwinding would strand. */
static int user_page_ok(const proc_t *pr, uint32_t pg) {
    if (paging_is_mapped(pg)) return 1;
    if (pr && pg >= USER_HEAP && pg < pr->brk) return 1;
    return pg >= USTACK_GUARD + 4096 && pg < USTACK_TOP;
}
static int user_ok(uint32_t ptr, uint32_t len) {
    if (ptr < USER_LO || ptr >= USER_HI) return 0;
    if (len > USER_HI - ptr) return 0;
    if (!len) return 1;
    proc_t *pr = proc_current();
    uint32_t end = (ptr + len - 1) & ~0xFFFu;
    for (uint32_t pg = ptr & ~0xFFFu; ; pg += 4096) {
        if (!user_page_ok(pr, pg)) return 0;
        if (pg == end) return 1;
    }
}

/* A NUL-terminated path out of user memory, each byte checked. 0, or -1 for
 * a bad pointer. Too long is cut at FAT_PATH_MAX - 1. */
static int copy_user_path(uint32_t up, char path[FAT_PATH_MAX]) {
    uint32_t i = 0;
    for (; i < FAT_PATH_MAX - 1; i++) {
        if (!user_ok(up + i, 1)) return -1;
        path[i] = *(const char *)(up + i);
        if (!path[i]) return 0;
    }
    path[i] = '\0';
    return 0;
}

/* ---- ring-0 resume point (called from usermode_asm.asm) ------------------
 * user_mode_enter hands us the kernel esp to come back to. It is recorded on
 * the task, so each ring-3 program unwinds into its own usermode_run, and
 * loaded into tss.esp0, so a trap from ring 3 lands on this task's kernel
 * stack rather than on a stack another process is already using. task_yield
 * keeps tss.esp0 in step with `current` from here on. */
static uint32_t g_resume_no_task;        /* fallback before task_init() */

void usermode_arm_resume(uint32_t esp) {
    task_t *me = task_current();
    if (me) me->user_resume_esp = esp;
    else    g_resume_no_task    = esp;

    proc_t *pr = proc_current();
    if (pr && pr->esp0 != esp) {
        pr->esp0 = esp;
        /* Logged because it is the whole invariant in one number: two live
         * processes must show two different values here. */
        klog("[proc %u] ring3 esp0=0x%x\n", pr->pid, esp);
    }
    tss_set_esp0(esp);
}

uint32_t usermode_resume_esp(void) {
    task_t *me = task_current();
    if (me && me->user_resume_esp) return me->user_resume_esp;
    return g_resume_no_task;
}

/* Map one fresh, zeroed, user-accessible page at `va` in the current address
 * space, which is this process's.
 *
 * Deduplication asks the address space itself. The old side table (1024
 * virtual addresses, scanned linearly) cost 4 KB of statics and an O(n) walk
 * per page, and could only ever describe one process; the page tables already
 * hold the answer and are authoritative. What remains of the counter is just
 * the per-process page budget. */
static int map_user_page(proc_t *pr, uint32_t va);

int usermode_demand_page(uint32_t cr2, uint32_t err) {
    proc_t *pr = proc_current();
    if (!pr) return -1;
    if (err & 1) return -1;                             /* present: a protection fault, real */
    /* The stack grows down on demand to 1 MB. Its lowest page is the guard:
     * never mapped, so running off the end is a clean kill with its own
     * name instead of a silent walk into the heap below. */
    if (cr2 >= USTACK_GUARD && cr2 < USTACK_GUARD + 4096) {
        klog("[user] %s: stack overflow at 0x%x (guard page)\n", pr->name, cr2);
        return -1;
    }
    if (cr2 > USTACK_GUARD && cr2 < USTACK_TOP) {
        if (pr->upages >= PROC_PAGE_CAP && swap_out_one(pr) < 0) return -1;
        if (map_user_page(pr, cr2 & ~0xFFFu) < 0) return -1;
        pr->stack_pages++;
        return 0;
    }
    if (cr2 < USER_HEAP || cr2 >= pr->brk) return -1;   /* outside the heap: real */

    /* On disk already? Then this is a page coming back, not a new one. */
    int back = swap_in(pr, cr2);
    if (back == 1) return 0;
    if (back < 0) {
        klog("[demand] %s: could not swap 0x%x back in\n", pr->name, cr2);
        return -1;
    }

    /* A new page. At the cap, make room by putting the oldest one on disk;
     * with no swap, or none left, that is the end of the program. */
    if (pr->upages >= PROC_PAGE_CAP && swap_out_one(pr) < 0) {
        klog("[demand] %s: no page for 0x%x (%u mapped, cap %u, swap %u of %u)\n",
             pr->name, cr2, pr->upages, (unsigned)PROC_PAGE_CAP,
             swap_slots_used(), swap_slots_total());
        return -1;
    }
    if (map_user_page(pr, cr2 & ~0xFFFu) < 0) {
        klog("[demand] %s: no frame for 0x%x (%u mapped)\n", pr->name, cr2, pr->upages);
        return -1;
    }
    swap_note_resident(pr, cr2 & ~0xFFFu);
    pr->demand_pages++;
    return 0;
}

static int map_user_page(proc_t *pr, uint32_t va) {
    if (paging_is_mapped(va)) return 0;                /* already backed */
    if (pr && pr->upages >= PROC_PAGE_CAP) return -1;
    void *frame = pmm_alloc_page();
    if (!frame) return -1;
    paging_map(va, (uint32_t)frame, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    memset((void *)va, 0, 4096);                       /* kernel write, U page */
    if (pr) pr->upages++;
    return 0;
}

/* Leave a process address space and reclaim it.
 *
 * paging_free_dir frees every present page in the user half, so there must be
 * no second teardown pass: both running would double-free the same frames. */
static void leave_proc_space(uint32_t *kdir, uint32_t *pdir) {
    task_t *me = task_current();
    if (me) me->page_dir = kdir;
    paging_switch(kdir);
    paging_free_dir(pdir);
}

static int load_segment(proc_t *pr, const uint8_t *buf, uint32_t bufsz,
                        const elf32_phdr_t *ph) {
    uint32_t va_start = ph->p_vaddr & ~0xFFFu;
    uint32_t va_end   = (ph->p_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;

    /* A program header names where to write. Check it lands in user space
     * before trusting it: map_user_page() happily maps any address, and for
     * an already-mapped kernel page it returns "already backed" and the
     * memcpy below then writes straight into the kernel. Reject overflow too,
     * so p_vaddr + p_memsz cannot wrap past the end and look in-range. */
    if (va_end <= va_start) return -1;                       /* wrapped */
    if (ph->p_vaddr < USER_LO) return -1;
    if (va_end > USER_HI)      return -1;

#ifdef NO_CHALLENGE
    /* And check where it reads FROM. p_offset is a file offset, so it must
     * land inside the file we actually loaded; without this the memcpy below
     * walks off the end of buf and copies arbitrary kernel memory into the
     * program's own image. Checked for overflow first so p_offset + p_filesz
     * cannot wrap and look in range. */
    if (ph->p_offset > bufsz) return -1;
    if (ph->p_filesz > bufsz - ph->p_offset) return -1;
#else
    /* CHALLENGE=1: p_offset is deliberately NOT validated. The loader checks
     * where it writes but not where it reads from, which is stage 3. */
    (void)bufsz;
#endif
    for (uint32_t va = va_start; va < va_end; va += 4096)
        if (map_user_page(pr, va) < 0) return -1;
    /* Zero the whole span (covers .bss), then drop in the file image. */
    memset((void *)va_start, 0, va_end - va_start);
    memcpy((void *)ph->p_vaddr, buf + ph->p_offset, ph->p_filesz);
    return 0;
}

/* Load pr->name and run it in ring 3 on the calling task. Returns the
 * program's exit code, or -1 if it could not be loaded.
 *
 * This was exec_elf(path, args). It takes a process now, and the task it runs
 * on belongs to that process, so several of these can be in flight at once.
 * From the caller's point of view it is still synchronous: user_mode_enter
 * returns when the program exits, faults, or is killed. */
int usermode_run(proc_t *pr) {
    if (!pr) return -1;

    uint8_t *buf = (uint8_t *)kmalloc(MAX_ELF);
    if (!buf) return -1;

    uint32_t size = 0;
    /* fat_read fills at most bufsize but reports the FILE's size: a program bigger than the buffer would pass every header
     * bounds check below against bytes that were never read. Refuse it. */
    if (fat_read(pr->name, buf, MAX_ELF, &size) < 0 || size > MAX_ELF ||
        size < sizeof(elf32_ehdr_t)) {
        kfree(buf); return -1;
    }

    elf32_ehdr_t *eh = (elf32_ehdr_t *)buf;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F' ||
        eh->e_machine != EM_386 || eh->e_type != ET_EXEC) {
        kfree(buf); return -1;
    }

    /* Give the program its own address space. Recorded on the task as well as
     * loaded into CR3, so that if the timer preempts us mid-program the
     * scheduler puts this directory back when we are resumed. */
    uint32_t *kdir = paging_kernel_dir();
    uint32_t *pdir = paging_new_dir();
    if (!pdir) { kfree(buf); return -1; }
    task_t *me_task = task_current();
    if (me_task) me_task->page_dir = pdir;
    paging_switch(pdir);

    pr->upages = 0;
    pr->demand_pages = 0;     /* a reused slot must not carry the last program's counts */
    pr->swapped      = 0;
    pr->stack_pages  = 0;
    pr->brk    = USER_HEAP;
    /* From 3 up only: 0-2 may hold the stdin/stdout the spawner bound (a pipe
     * end or a redirected file), and clearing them here would quietly undo
     * every redirection. */
    for (int i = 3; i < PROC_UFD_MAX; i++) pr->ufds[i] = 0;

    elf32_phdr_t *ph = (elf32_phdr_t *)(buf + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        if (load_segment(pr, buf, size, &ph[i]) < 0) {
            leave_proc_space(kdir, pdir); kfree(buf); return -1;
        }
    }

    /* User stack. */
    for (int i = 0; i < USTACK_PAGES; i++) {
        if (map_user_page(pr, USTACK_TOP - (uint32_t)(i + 1) * 4096) < 0) {
            leave_proc_space(kdir, pdir); kfree(buf); return -1;
        }
    }

    uint32_t entry = eh->e_entry;
    kfree(buf);                       /* segments are already copied out */

    pr->exit_code = 0;
    pr->in_user   = 1;
    user_mode_enter(entry, USTACK_TOP);   /* returns on exit, fault or kill */
    pr->in_user   = 0;

    /* Out of ring 3: the frame user_resume_esp pointed at is gone, so stop
     * advertising it as a trap target. */
    if (me_task) me_task->user_resume_esp = 0;
    pr->esp0 = 0;
    tss_set_esp0(tss_boot_esp0());

    for (int i = 0; i < PROC_UFD_MAX; i++) { /* close anything left open */
        if (!pr->ufds[i]) continue;
        char name[VFS_NAME_MAX];
        strncpy(name, pr->ufds[i]->name, sizeof(name) - 1); name[sizeof(name) - 1] = '\0';
        if (vfs_close(pr->ufds[i]) < 0 && pr->ufds[i]->type == VFS_FILE) {
            /* A file written through this handle did not reach the disk
             * whole (v0.56.4). Say so where the cook can see it, and do not
             * let the program's 0 pretend it all worked. */
            term_printf(term_current(), "  [%s: the output to /%s did not reach the disk whole]\n", pr->name, name);
            klog("[proc %u] %s: output to /%s cut short\n", pr->pid, pr->name, name);
            if (pr->exit_code == 0) pr->exit_code = 1;
        }
        pr->ufds[i] = 0;
    }
    leave_proc_space(kdir, pdir);
    return pr->exit_code;
}

/* stdin for a ring-3 program.
 *
 * Only the foreground process owns the keyboard; any other process reads EOF
 * rather than stealing keys from the shell or from the program the user is
 * actually talking to. Unlike keyboard_getchar() this is interruptible: a kill
 * flagged while a program sits waiting for input has to be noticed here,
 * because the Ctrl-C that flagged it was swallowed by the keyboard layer and
 * no key is coming to wake us.
 *
 * Returns the character, or -1 for EOF. */
static int user_stdin_getchar(proc_t *pr) {
    if (!pr) return -1;
    /* A program cooked on another terminal (an SSH session) reads that
     * terminal, and only while it is that terminal's foreground program. */
    term_t *t = (term_t *)pr->term;
    if (t && t != &term_vga) {
        if (t->fg != pr) return -1;
        __asm__ volatile ("sti");
        while (!t->available(t)) {
            if (pr->killed) return -1;
            task_yield();
            if (!t->available(t) && !pr->killed) cpu_halt();
        }
        return pr->killed ? -1 : t->getc(t);
    }
    if (pr != proc_foreground()) return -1;             /* not ours: EOF */

    /* int 0x80 is a DPL-3 *interrupt* gate, so IF is clear in here. Waiting
     * for a key with interrupts off would wait forever: the keyboard IRQ that
     * fills the buffer could never fire. Safe to enable now that each task
     * traps onto its own ring-0 stack (task_t.user_resume_esp), so another
     * ring3->ring0 trap while we are switched away cannot clobber our frame.
     * The user's own IF is restored from its saved EFLAGS by iret. */
    __asm__ volatile ("sti");

    while (!keyboard_available()) {
        if (pr->killed) return -1;
        task_yield();
        if (!keyboard_available() && !pr->killed)
            cpu_halt();
    }
    return keyboard_getchar();
}

/* ---- syscall dispatch (called from isr_stubs.asm, int 0x80) ---- */
void syscall_dispatch(registers_t *r) {
    proc_t *pr = proc_current();

    /* A pending stop or kill takes effect here, at the syscall boundary, where
     * the kernel holds no locks: the trap frame can be abandoned safely and
     * parking the task cannot strand a mutex. Must stay before anything that
     * takes a lock or allocates. */
    if (pr && pr->stop_pending) proc_take_stop();       /* parks until resumed */
    if (pr && pr->killed) usermode_killed("syscall");   /* does not return */

    switch (r->eax) {
        case SYS_EXIT:
            if (pr) pr->exit_code = (int)r->ebx;
            user_mode_exit();             /* does not return */
            break;

        case SYS_WRITE: {
            uint32_t fd = r->ebx, len = r->edx;
            const char *p = (const char *)r->ecx;
            if (!user_ok(r->ecx, len)) { r->eax = (uint32_t)-1; break; }
            if (fd == FD_STDERR && pr && pr->err_as_out) fd = FD_STDOUT;   /* 2>&1 */
            int to_term = (pr && pr->err_as_out == 2 && r->ebx == FD_STDERR);   /* 2>&1 > f */
            /* A binding wins over the console default, which is what makes
             * `> file` and `| next` work at all. Nothing binds 0-2 for a
             * program run normally, so normal runs are unaffected. */
            if (!to_term && pr && fd < PROC_UFD_MAX && pr->ufds[fd]) {
                int w = vfs_write(pr->ufds[fd], p, len);
                r->eax = (uint32_t)(w < 0 ? -1 : w);
            } else if (fd == FD_STDOUT) {
                /* The terminal it was cooked on: the console, or a session. */
                term_t *t = term_current();
                for (uint32_t i = 0; i < len; i++) t->putc(t, p[i]);
                r->eax = len;
            } else if (fd == FD_STDERR) {
                /* The console's stderr is the serial log (the gate reads it).
                 * A program in an SSH session writes stderr to the session:
                 * the log is the console's, not every cook's. */
                term_t *t = term_current();
                /* Inside a $(...) the terminal is a capture: stdout only is
                 * captured, as sh does, so stderr goes on to the terminal it
                 * replaced (v0.60.3). */
                if (t->name && strcmp(t->name, "subst") == 0 && t->ctx) t = (term_t *)t->ctx;
                if (t != &term_vga) for (uint32_t i = 0; i < len; i++) t->putc(t, p[i]);
                else                for (uint32_t i = 0; i < len; i++) serial_putc(p[i]);
                r->eax = len;
            } else {
                r->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_READ: {
            uint32_t fd = r->ebx, max = r->edx;
            char *p = (char *)r->ecx;
            if (!user_ok(r->ecx, max)) { r->eax = (uint32_t)-1; break; }
            if (pr && fd < PROC_UFD_MAX && pr->ufds[fd]) {
                int got = vfs_read(pr->ufds[fd], p, max);
                r->eax = (uint32_t)(got < 0 ? 0 : got);
            } else if (fd == FD_STDIN) {
                uint32_t n = 0;
                if (max > 0) {
                    int c = user_stdin_getchar(pr);
                    if (c >= 0 && c < 256) p[n++] = (char)c;
                }
                r->eax = n;               /* 0 = EOF */
            } else {
                r->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_OPEN: {
            /* The path is copied out a byte at a time, each checked: until
             * v0.48.0 only its first byte was, and vfs_open read the rest
             * wherever it ran. */
            char path[FAT_PATH_MAX];
            if (!pr || copy_user_path(r->ebx, path) < 0) { r->eax = (uint32_t)-1; break; }
            int flags = (r->ecx == O_WRITE)  ? (VFS_WRONLY | VFS_CREATE) :
                        (r->ecx == O_APPEND) ? (VFS_WRONLY | VFS_APPEND) : VFS_RDONLY;
#ifdef NO_CHALLENGE
            /* The shell's rule (users_may), applied to a program's open. Only
             * in the ordinary build: in the CHALLENGE build an unchecked open
             * is part of what the challenge teaches, and stays. */
            {
                int writing = (r->ecx == O_WRITE || r->ecx == O_APPEND), ok;
                if (!writing)                 ok = users_may(path, 'r');
                else if (fat_exists(path))    ok = users_may(path, 'w');
                else {
                    char parent[FAT_PATH_MAX];
                    int n = (int)strlen(path);
                    while (n > 1 && path[n - 1] != '/') n--;
                    if (n > 1) n--;
                    if (n < 1) n = 1;
                    memcpy(parent, path, (uint32_t)n); parent[n] = '\0';
                    if (path[0] != '/') strcpy(parent, "/");
                    ok = users_may(parent, 'w');
                }
                if (!ok) {
                    klog("[user] %s: open %s for %s refused\n", pr->name, path, writing ? "writing" : "reading");
                    r->eax = (uint32_t)-1; break;
                }
            }
#endif
            int slot = -1;
            for (int i = 3; i < PROC_UFD_MAX; i++) if (!pr->ufds[i]) { slot = i; break; }
            if (slot < 0) { r->eax = (uint32_t)-1; break; }
            vfs_node_t *nd = vfs_open(path, flags);
            if (!nd) { r->eax = (uint32_t)-1; break; }
            pr->ufds[slot] = nd;
            r->eax = (uint32_t)slot;
            break;
        }

        case SYS_CLOSE: {
            uint32_t fd = r->ebx;
            if (pr && fd >= 3 && fd < PROC_UFD_MAX && pr->ufds[fd]) {
                vfs_close(pr->ufds[fd]); pr->ufds[fd] = 0; r->eax = 0;
            } else {
                r->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_SBRK: {
            if (!pr) { r->eax = (uint32_t)-1; break; }
            int incr = (int)r->ebx;
            uint32_t old = pr->brk;
            if (incr < 0) {
                pr->brk = (old < (uint32_t)(-incr) || old + incr < USER_HEAP)
                              ? USER_HEAP : old + (uint32_t)incr;
                r->eax = old; break;
            }
            uint32_t newbrk = old + (uint32_t)incr;
            if (newbrk > USTACK_GUARD) {
                r->eax = (uint32_t)-1; break;   /* would hit the stack */
            }
            /* The break moves; no page is mapped until it is touched. A
             * program can now claim far more than the machine would give it
             * at once, and only pays for what it uses - see
             * usermode_demand_page for the bill. */
            pr->brk = newbrk;
            r->eax = old;
            break;
        }

        case SYS_ARGS: {
            char *p = (char *)r->ebx;
            uint32_t max = r->ecx;
            if (!pr || !user_ok(r->ebx, max) || max == 0) { r->eax = (uint32_t)-1; break; }
            uint32_t n = 0;
            while (n < max - 1 && pr->args[n]) { p[n] = pr->args[n]; n++; }
            p[n] = '\0';
            r->eax = n;
            break;
        }

        case SYS_ENV: {                   /* as SYS_ARGS, for the environment (v0.60.26) */
            char *p = (char *)r->ebx;
            uint32_t max = r->ecx;
            if (!pr || !user_ok(r->ebx, max) || max == 0) { r->eax = (uint32_t)-1; break; }
            uint32_t n = 0;
            while (n < max - 1 && pr->env[n]) { p[n] = pr->env[n]; n++; }
            p[n] = '\0';
            r->eax = n;
            break;
        }

        case SYS_YIELD:
            /* Hand the CPU to other ready tasks. We trapped through a DPL-3
             * *interrupt* gate, so IF is clear right now; a peer that hlts
             * waiting for an IRQ would deadlock with interrupts off, so
             * re-enable them before switching. This used to be safe only
             * because one user program ran at a time, so the frame left on
             * the TSS esp0 stack could not be clobbered. It is now safe by
             * construction: esp0 follows `current`, so every task traps onto
             * its own kernel stack. The user's IF is restored from its saved
             * EFLAGS by iret. */
            __asm__ volatile ("sti");
            task_yield();
            r->eax = 0;
            break;

        case SYS_UNLINK: {
            /* A program calls the FAT directly, so the check the shell would
             * make is made here, by the same rule (users_may): write
             * permission on the file, as `strain` asks. The caller's user is
             * its shell's (users.c resolves it through the task). A bowl is
             * not a file: -1. */
            char path[FAT_PATH_MAX];
            uint8_t owner = 0, mode = 0;
            if (copy_user_path(r->ebx, path) < 0 || !path[0] || fat_is_dir(path) ||
                fat_stat(path, &owner, &mode) < 0) { r->eax = (uint32_t)-1; break; }
            if (!users_may(path, 'w')) {               /* what `strain` asks */
                klog("[user] %s: unlink %s refused (owner %u)\n", pr ? pr->name : "?", path, owner);
                r->eax = (uint32_t)-1; break;
            }
            r->eax = (uint32_t)(fat_delete(path) < 0 ? -1 : 0);
            break;
        }

        case SYS_MKDIR: {
            char path[FAT_PATH_MAX];
            if (copy_user_path(r->ebx, path) < 0 || !path[0]) { r->eax = (uint32_t)-1; break; }
            /* What `mkbowl` asks: may the caller write in the parent? */
            char parent[FAT_PATH_MAX];
            int n = (int)strlen(path);
            while (n > 1 && path[n - 1] != '/') n--;
            if (n > 1) n--;
            if (n < 1) n = 1;
            memcpy(parent, path, (uint32_t)n); parent[n] = '\0';
            if (path[0] != '/') strcpy(parent, "/");
            if (!users_may(parent, 'w')) {
                klog("[user] %s: mkdir %s refused (no write in %s)\n", pr ? pr->name : "?", path, parent);
                r->eax = (uint32_t)-1; break;
            }
            r->eax = (uint32_t)(fat_mkdir(path) < 0 ? -1 : 0);
            break;
        }

        case SYS_STAT: {
            /* What a path is (v0.59.0): size, bowl or not, owner, mode. The
             * cook needs search on the bowls above it, not read on it, as
             * with stat - so taste.elf -f sees a file it may not read. */
            char path[FAT_PATH_MAX];
            if (copy_user_path(r->ebx, path) < 0) { r->eax = (uint32_t)-1; break; }
            if (!user_ok(r->ecx, sizeof(ustat_t))) { r->eax = (uint32_t)-1; break; }
#ifdef NO_CHALLENGE
            if (!users_reach(path)) { r->eax = (uint32_t)-1; break; }
#endif
            uint8_t owner, mode;
            if (!fat_exists(path) || fat_stat(path, &owner, &mode) < 0) { r->eax = (uint32_t)-1; break; }
            ustat_t st;
            st.is_dir = fat_is_dir(path) ? 1u : 0u;
            st.size   = 0;
            if (!st.is_dir) { int fd = fat_fopen(path); if (fd >= 0) { st.size = fat_fsize(fd); fat_fclose(fd); } }
            st.owner  = owner;
            st.mode   = mode & 0x3F;
            memset(st.owner_name, 0, sizeof(st.owner_name));
            strncpy(st.owner_name, users_name_of(owner), sizeof(st.owner_name) - 1);
            memcpy((void *)r->ecx, &st, sizeof(st));
            r->eax = 0;
            break;
        }
        case SYS_READDIR: {
            /* Entry `index` of a directory. The path is copied out of user
             * memory a byte at a time, each byte checked, and the listing is
             * made into a per-call allocation, not a static: two programs may
             * be listing at once. Index past the end gives 0, so a program
             * loops until it sees that. */
            char path[FAT_PATH_MAX];
            if (copy_user_path(r->ebx, path) < 0) { r->eax = (uint32_t)-1; break; }
            if (!user_ok(r->edx, sizeof(udirent_t))) { r->eax = (uint32_t)-1; break; }
#ifdef NO_CHALLENGE
            /* Listing a bowl needs read on it, and search on the bowls above
             * (v0.53.1); a closed home was listable by any program. */
            if (!users_may(path[0] ? path : "/", 'r')) {
                klog("[user] %s: readdir %s refused\n", pr ? pr->name : "?", path);
                r->eax = (uint32_t)-1; break;
            }
#endif
            /* entry idx alone (v0.60.125): it was the first FAT_LS_MAX
             * listed and indexed, so a bowl of more ended at 128 */
            fat_entry_t *list = kmalloc(sizeof(fat_entry_t));
            if (!list) { r->eax = (uint32_t)-1; break; }
            uint32_t idx = r->ecx;
            int n = idx > 0x7fffffff ? 0 : fat_ls_from(path[0] ? path : "/", list, 1, (int)idx);
            if (n < 0)                 r->eax = (uint32_t)-1;
            else if (n == 0)           r->eax = 0;
            else {
                idx = 0;
                udirent_t *out = (udirent_t *)r->edx;
                const char *nm = fat_display_name(&list[idx]);
                uint32_t k = 0;
                for (; nm[k] && k < sizeof(out->name) - 1; k++) out->name[k] = nm[k];
                out->name[k] = '\0';
                out->size   = list[idx].size;
                out->is_dir = (list[idx].attr & FAT_ATTR_DIR) ? 1 : 0;
                r->eax = 1;
            }
            kfree(list);
            break;
        }
        case SYS_TICKS:                   /* uptime in 100 Hz PIT ticks */
            r->eax = timer_get_ticks();
            break;
        case SYS_SLEEP:                   /* (v0.60.101) the task sleeps, ten seconds at most a call */
            /* The interrupt gate cleared IF, and only the timer's ticks wake a
             * sleeper: on again first, as SYS_YIELD does. */
            __asm__ volatile ("sti");
            task_sleep(r->ebx > 10000 ? 10000 : r->ebx);
            r->eax = 0;
            break;
        case SYS_RANDOM: {                /* (v0.60.118) what $RANDOM draws from, for tumble.elf */
            uint32_t len = r->ecx > 256 ? 256 : r->ecx;
            if (!user_ok(r->ebx, len)) { r->eax = (uint32_t)-1; break; }
            random_bytes((void *)r->ebx, len);
            r->eax = len;
            break;
        }
        case SYS_WIN_OPEN: {              /* (v0.60.146) a window on the desktop */
            char title[FAT_PATH_MAX];
            if (!pr || copy_user_path(r->edx, title) < 0) { r->eax = (uint32_t)-1; break; }
            r->eax = (uint32_t)countertop_win_open(pr->pid, (int)r->ebx, (int)r->ecx, title);
            break;
        }
        case SYS_WIN_PUT: {               /* its whole body, copied in: the desktop draws from the copy */
            int w, h;
            if (!pr || countertop_win_size(pr->pid, (int)r->ebx, &w, &h) < 0
                    || !user_ok(r->ecx, (uint32_t)(w * h * 4))) { r->eax = (uint32_t)-1; break; }
            r->eax = (uint32_t)countertop_win_put(pr->pid, (int)r->ebx, (const uint32_t *)r->ecx);
            break;
        }
        case SYS_WIN_EVENT: {             /* the next event, waiting up to edx ms for one */
            if (!pr || !user_ok(r->ecx, sizeof(uwinev_t))) { r->eax = (uint32_t)-1; break; }
            __asm__ volatile ("sti");     /* the timer wakes the sleep, as SYS_SLEEP */
            uint32_t ms = r->edx > 10000 ? 10000 : r->edx, t0 = timer_get_ticks();
            uwinev_t ev;
            int got;
            for (;;) {
                got = countertop_win_event(pr->pid, (int)r->ebx, &ev);
                if (got || pr->killed || pr->stop_pending || (timer_get_ticks() - t0) * 10 >= ms) break;
                task_sleep(10);
            }
            if (got == 1) memcpy((void *)r->ecx, &ev, sizeof(ev));
            r->eax = (uint32_t)got;
            break;
        }
        case SYS_WIN_CLOSE:
            r->eax = (uint32_t)(pr ? countertop_win_close(pr->pid, (int)r->ebx) : -1);
            break;

        default:
            r->eax = (uint32_t)-1;
    }

    /* A stop or kill can also arrive *during* a syscall: the FAT mutex yields,
     * and stdin waits. Checking again on the way out bounds the latency to one
     * syscall without ever unwinding from inside a lock. */
    if (pr && pr->stop_pending) proc_take_stop();
    if (pr && pr->killed) usermode_killed("syscall-exit");  /* no return */
}

void usermode_selftest(void) {
    const char *msg = "[syscall] ring0 int 0x80 -> serial ok\n";
    uint32_t len = (uint32_t)strlen(msg);
    __asm__ volatile ("int $0x80"
                      : : "a"(SYS_WRITE), "b"(2), "c"(msg), "d"(len)
                      : "memory");
}

/* ---- ring-3 fault handling ------------------------------------------------
 * A fault in a user program must kill the program, not the machine. Ring 3
 * exists precisely so a bad program cannot take the kernel down; before this,
 * `cook`ing an ELF with a bad opcode panicked and halted the whole box. */

int usermode_in_user(void) {
    proc_t *pr = proc_current();
    return pr && pr->in_user;
}

void usermode_fault(uint32_t exc, uint32_t err, uint32_t eip, uint32_t cr2) {
    g_fault_exc = exc; g_fault_err = err;
    g_fault_eip = eip; g_fault_cr2 = cr2;

    proc_t *pr = proc_current();
    if (pr) {
        pr->exit_code = -(int)(128u + exc);
        pr->in_user   = 0;
    }
    user_mode_exit();                  /* unwinds into usermode_run; no return */
}

/* Tear down a process that proc_kill flagged. Reached only from a point where
 * the kernel holds no locks: syscall entry or exit, or a timer IRQ that
 * interrupted ring-3 code. Unwinds through the same path as a fault, so the
 * address space, the fds and the task all come down the normal way. */
void usermode_killed(const char *where) {
    proc_t *pr = proc_current();
    task_t *me = task_current();
    /* Nothing to unwind to: refuse rather than jump to esp 0. */
    if (!pr || !me || !me->user_resume_esp) return;

    pr->exit_code = PROC_EXIT_KILLED;
    pr->in_user   = 0;
    pr->killed    = 0;                 /* acted on */
    klog("[proc %u] killed (%s)\n", pr->pid, where ? where : "?");
    user_mode_exit();                  /* no return */
}

void usermode_last_fault(uint32_t *exc, uint32_t *err,
                         uint32_t *eip, uint32_t *cr2) {
    if (exc) *exc = g_fault_exc;
    if (err) *err = g_fault_err;
    if (eip) *eip = g_fault_eip;
    if (cr2) *cr2 = g_fault_cr2;
}

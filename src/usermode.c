/* usermode.c - the ring-3 / syscall / ELF-loader side of OS-ification step 3.
 *
 * A loaded program lives in high virtual space (USER_BASE and up), well above
 * the identity-mapped low RAM, backed by fresh physical pages marked
 * user-accessible. There is no per-process page directory yet: user pages are
 * added to the single kernel page directory and torn down when the program
 * exits, so only one user program runs at a time (synchronously, from the
 * shell). Memory isolation between user and kernel is real (the U/S bit);
 * isolation *between* user programs is future work.
 */
#include "usermode.h"
#include "syscall_nr.h"
#include "isr.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "fat.h"
#include "str.h"
#include "vga.h"
#include "serial.h"
#include "keyboard.h"
#include "gdt.h"
#include "klog.h"
#include "vfs.h"
#include "task.h"
#include "timer.h"

/* ---- user address-space layout ----
 *   0xC0000000  program image (PT_LOAD)
 *   0xC4000000  heap (grows up, via sbrk)
 *   0xC8000000  top of stack (grows down)
 * All above the identity-mapped low RAM, so there is no aliasing. */
#define USER_LO       0xC0000000u
#define USER_HI       0xC8000000u
#define USER_HEAP     0xC4000000u
#define USTACK_TOP    0xC8000000u    /* top of the user stack            */
#define USTACK_PAGES  4              /* 16 KB stack                      */
#define MAX_ELF       (256 * 1024)   /* largest program we'll load       */
#define MAX_UPAGES    1024           /* track up to 4 MB of user pages   */

#define MAX_UFD       16             /* per-process open files (fd >= 3)  */
#define UARGS_MAX     128            /* command-line args passed to cook  */

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

/* ---- per-run state ---- */
static int          g_exit_code;
/* Set while a ring-3 program is actually running, so the exception handler can
 * tell a user fault (kill the program) from a kernel fault (panic). */
static volatile int g_user_active;
static uint32_t     g_fault_exc, g_fault_err, g_fault_eip, g_fault_cr2;
static uint32_t     mapped[MAX_UPAGES];   /* page-aligned virt addrs we mapped */
static int          nmapped;
static uint32_t     g_user_brk;           /* current heap break              */
static vfs_node_t  *g_ufds[MAX_UFD];      /* open files; index >= 3 used     */
static char         g_user_args[UARGS_MAX];

/* A user pointer is acceptable only if it lies entirely within user space, so
 * a buggy/hostile program cannot make the kernel read or write elsewhere. */
static int user_ok(uint32_t ptr, uint32_t len) {
    if (ptr < USER_LO || ptr >= USER_HI) return 0;
    if (len > USER_HI - ptr) return 0;
    return 1;
}

static void track_reset(void) { nmapped = 0; }

static int is_mapped(uint32_t va) {
    for (int i = 0; i < nmapped; i++) if (mapped[i] == va) return 1;
    return 0;
}

/* Map one fresh, zeroed, user-accessible page at virtual address `va`. */
static int map_user_page(uint32_t va) {
    if (is_mapped(va)) return 0;                  /* already backed */
    if (nmapped >= MAX_UPAGES) return -1;
    void *frame = pmm_alloc_page();
    if (!frame) return -1;
    paging_map(va, (uint32_t)frame, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    memset((void *)va, 0, 4096);                  /* kernel write, U page */
    mapped[nmapped++] = va;
    return 0;
}

/* Leave a process address space and reclaim it.
 *
 * paging_free_dir frees every present page in the user half, so
 * teardown_user_pages must NOT also run: they would double-free the same
 * frames. The page tracker is only kept now for map_user_page's dedup and
 * its MAX_UPAGES cap, so resetting the count is all that is needed. */
static void leave_proc_space(uint32_t *kdir, uint32_t *pdir) {
    task_t *me = task_current();
    if (me) me->page_dir = kdir;
    paging_switch(kdir);
    paging_free_dir(pdir);
    nmapped = 0;
}


static int load_segment(const uint8_t *buf, uint32_t bufsz,
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
        if (map_user_page(va) < 0) return -1;
    /* Zero the whole span (covers .bss), then drop in the file image. */
    memset((void *)va_start, 0, va_end - va_start);
    memcpy((void *)ph->p_vaddr, buf + ph->p_offset, ph->p_filesz);
    return 0;
}

int exec_elf(const char *path, const char *args) {
    uint8_t *buf = (uint8_t *)kmalloc(MAX_ELF);
    if (!buf) return -1;

    uint32_t size = 0;
    if (fat_read(path, buf, MAX_ELF, &size) < 0 || size < sizeof(elf32_ehdr_t)) {
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

    track_reset();
    for (int i = 0; i < MAX_UFD; i++) g_ufds[i] = 0;
    g_user_brk = USER_HEAP;
    g_user_args[0] = '\0';
    if (args) { strncpy(g_user_args, args, UARGS_MAX - 1); g_user_args[UARGS_MAX - 1] = '\0'; }

    elf32_phdr_t *ph = (elf32_phdr_t *)(buf + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        if (load_segment(buf, size, &ph[i]) < 0) {
            leave_proc_space(kdir, pdir); kfree(buf); return -1;
        }
    }

    /* User stack. */
    for (int i = 0; i < USTACK_PAGES; i++) {
        if (map_user_page(USTACK_TOP - (uint32_t)(i + 1) * 4096) < 0) {
            leave_proc_space(kdir, pdir); kfree(buf); return -1;
        }
    }

    uint32_t entry = eh->e_entry;
    kfree(buf);                       /* segments are already copied out */

    g_exit_code = 0;
    g_user_active = 1;
    user_mode_enter(entry, USTACK_TOP);   /* returns after exit, or a fault */
    g_user_active = 0;

    for (int i = 0; i < MAX_UFD; i++)     /* close anything left open */
        if (g_ufds[i]) { vfs_close(g_ufds[i]); g_ufds[i] = 0; }
    leave_proc_space(kdir, pdir);
    return g_exit_code;
}

/* ---- syscall dispatch (called from isr_stubs.asm, int 0x80) ---- */
void syscall_dispatch(registers_t *r) {
    switch (r->eax) {
        case SYS_EXIT:
            g_exit_code = (int)r->ebx;
            user_mode_exit();             /* does not return */
            break;

        case SYS_WRITE: {
            uint32_t fd = r->ebx, len = r->edx;
            const char *p = (const char *)r->ecx;
            if (!user_ok(r->ecx, len)) { r->eax = (uint32_t)-1; break; }
            if (fd == FD_STDOUT) {
                for (uint32_t i = 0; i < len; i++) vga_putchar(p[i]);
                r->eax = len;
            } else if (fd == FD_STDERR) {
                for (uint32_t i = 0; i < len; i++) serial_putc(p[i]);
                r->eax = len;
            } else if (fd >= 3 && fd < MAX_UFD && g_ufds[fd]) {
                int w = vfs_write(g_ufds[fd], p, len);
                r->eax = (uint32_t)(w < 0 ? -1 : w);
            } else {
                r->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_READ: {
            uint32_t fd = r->ebx, max = r->edx;
            char *p = (char *)r->ecx;
            if (!user_ok(r->ecx, max)) { r->eax = (uint32_t)-1; break; }
            if (fd == FD_STDIN) {
                uint32_t n = 0;
                if (max > 0) { int c = keyboard_getchar(); if (c >= 0 && c < 256) p[n++] = (char)c; }
                r->eax = n;
            } else if (fd >= 3 && fd < MAX_UFD && g_ufds[fd]) {
                int got = vfs_read(g_ufds[fd], p, max);
                r->eax = (uint32_t)(got < 0 ? 0 : got);
            } else {
                r->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_OPEN: {
            const char *path = (const char *)r->ebx;
            if (!user_ok(r->ebx, 1)) { r->eax = (uint32_t)-1; break; }
            int flags = (r->ecx == O_WRITE) ? (VFS_WRONLY | VFS_CREATE) : VFS_RDONLY;
            int slot = -1;
            for (int i = 3; i < MAX_UFD; i++) if (!g_ufds[i]) { slot = i; break; }
            if (slot < 0) { r->eax = (uint32_t)-1; break; }
            vfs_node_t *nd = vfs_open(path, flags);
            if (!nd) { r->eax = (uint32_t)-1; break; }
            g_ufds[slot] = nd;
            r->eax = (uint32_t)slot;
            break;
        }

        case SYS_CLOSE: {
            uint32_t fd = r->ebx;
            if (fd >= 3 && fd < MAX_UFD && g_ufds[fd]) {
                vfs_close(g_ufds[fd]); g_ufds[fd] = 0; r->eax = 0;
            } else {
                r->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_SBRK: {
            int incr = (int)r->ebx;
            uint32_t old = g_user_brk;
            if (incr < 0) {
                g_user_brk = (old < (uint32_t)(-incr) || old + incr < USER_HEAP)
                                 ? USER_HEAP : old + (uint32_t)incr;
                r->eax = old; break;
            }
            uint32_t newbrk = old + (uint32_t)incr;
            if (newbrk > USTACK_TOP - (uint32_t)USTACK_PAGES * 4096) {
                r->eax = (uint32_t)-1; break;   /* would hit the stack */
            }
            for (uint32_t va = old & ~0xFFFu; va < newbrk; va += 4096) {
                if (map_user_page(va) < 0) { r->eax = (uint32_t)-1; goto sbrk_done; }
            }
            g_user_brk = newbrk;
            r->eax = old;
            sbrk_done: break;
        }

        case SYS_ARGS: {
            char *p = (char *)r->ebx;
            uint32_t max = r->ecx;
            if (!user_ok(r->ebx, max) || max == 0) { r->eax = (uint32_t)-1; break; }
            uint32_t n = 0;
            while (n < max - 1 && g_user_args[n]) { p[n] = g_user_args[n]; n++; }
            p[n] = '\0';
            r->eax = n;
            break;
        }

        case SYS_YIELD:
            /* Hand the CPU to other ready tasks (e.g. a bgclock). We trapped
             * through a DPL-3 *interrupt* gate, so IF is clear right now; a
             * peer that hlts while waiting for an IRQ would deadlock with
             * interrupts off, so re-enable them before switching. This is
             * safe: only one user program runs at a time, so the frame we
             * leave on the TSS (esp0) kernel stack can't be clobbered by
             * another ring3->ring0 trap while we're switched away. The final
             * IF state is restored from the user's saved EFLAGS by iret. */
            __asm__ volatile ("sti");
            task_yield();
            r->eax = 0;
            break;

        case SYS_TICKS:                   /* uptime in 100 Hz PIT ticks */
            r->eax = timer_get_ticks();
            break;

        default:
            r->eax = (uint32_t)-1;
    }
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

int usermode_in_user(void) { return g_user_active; }

void usermode_fault(uint32_t exc, uint32_t err, uint32_t eip, uint32_t cr2) {
    g_fault_exc = exc; g_fault_err = err;
    g_fault_eip = eip; g_fault_cr2 = cr2;
    g_exit_code = -(int)(128u + exc);
    g_user_active = 0;
    user_mode_exit();                  /* unwinds into exec_elf; no return */
}

void usermode_last_fault(uint32_t *exc, uint32_t *err,
                         uint32_t *eip, uint32_t *cr2) {
    if (exc) *exc = g_fault_exc;
    if (err) *err = g_fault_err;
    if (eip) *eip = g_fault_eip;
    if (cr2) *cr2 = g_fault_cr2;
}

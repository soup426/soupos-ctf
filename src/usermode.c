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
#include "proc.h"

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
 * a buggy/hostile program cannot make the kernel read or write elsewhere. */
static int user_ok(uint32_t ptr, uint32_t len) {
    if (ptr < USER_LO || ptr >= USER_HI) return 0;
    if (len > USER_HI - ptr) return 0;
    return 1;
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
    if (fat_read(pr->name, buf, MAX_ELF, &size) < 0 ||
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
    pr->brk    = USER_HEAP;
    for (int i = 0; i < PROC_UFD_MAX; i++) pr->ufds[i] = 0;

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

    for (int i = 0; i < PROC_UFD_MAX; i++)   /* close anything left open */
        if (pr->ufds[i]) { vfs_close(pr->ufds[i]); pr->ufds[i] = 0; }
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
    if (!pr || pr != proc_foreground()) return -1;      /* not ours: EOF */

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
            __asm__ volatile ("hlt");
    }
    return keyboard_getchar();
}

/* ---- syscall dispatch (called from isr_stubs.asm, int 0x80) ---- */
void syscall_dispatch(registers_t *r) {
    proc_t *pr = proc_current();

    /* A pending kill takes effect here, at the syscall boundary, where the
     * kernel holds no locks and the trap frame can be abandoned safely. Must
     * stay before anything that takes a lock or allocates. */
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
            if (fd == FD_STDOUT) {
                for (uint32_t i = 0; i < len; i++) vga_putchar(p[i]);
                r->eax = len;
            } else if (fd == FD_STDERR) {
                for (uint32_t i = 0; i < len; i++) serial_putc(p[i]);
                r->eax = len;
            } else if (pr && fd >= 3 && fd < PROC_UFD_MAX && pr->ufds[fd]) {
                int w = vfs_write(pr->ufds[fd], p, len);
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
                if (max > 0) {
                    int c = user_stdin_getchar(pr);
                    if (c >= 0 && c < 256) p[n++] = (char)c;
                }
                r->eax = n;               /* 0 = EOF */
            } else if (pr && fd >= 3 && fd < PROC_UFD_MAX && pr->ufds[fd]) {
                int got = vfs_read(pr->ufds[fd], p, max);
                r->eax = (uint32_t)(got < 0 ? 0 : got);
            } else {
                r->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_OPEN: {
            const char *path = (const char *)r->ebx;
            if (!pr || !user_ok(r->ebx, 1)) { r->eax = (uint32_t)-1; break; }
            int flags = (r->ecx == O_WRITE) ? (VFS_WRONLY | VFS_CREATE) : VFS_RDONLY;
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
            if (newbrk > USTACK_TOP - (uint32_t)USTACK_PAGES * 4096) {
                r->eax = (uint32_t)-1; break;   /* would hit the stack */
            }
            for (uint32_t va = old & ~0xFFFu; va < newbrk; va += 4096) {
                if (map_user_page(pr, va) < 0) { r->eax = (uint32_t)-1; goto sbrk_done; }
            }
            pr->brk = newbrk;
            r->eax = old;
            sbrk_done: break;
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

        case SYS_TICKS:                   /* uptime in 100 Hz PIT ticks */
            r->eax = timer_get_ticks();
            break;

        default:
            r->eax = (uint32_t)-1;
    }

    /* A kill can also arrive *during* a syscall: the FAT mutex yields, and
     * stdin waits. Checking again on the way out bounds the latency to one
     * syscall without ever unwinding from inside a lock. */
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

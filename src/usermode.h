#pragma once
#include <stdint.h>

struct proc;

/* Load the ELF32 executable named by the process, map its PT_LOAD segments
 * into fresh user pages of its own address space, and run it in ring 3 until
 * it exits, faults, or is killed. Returns the program's exit code, or -1 on a
 * load error.
 *
 * Runs on the calling task, which must be the process's own task: the ring-0
 * resume point and tss.esp0 are that task's. proc_spawn (proc.c) is what sets
 * that up; this is not called directly from the shell any more.
 *
 * Was exec_elf(path, args) before processes existed. */
int usermode_run(struct proc *pr);

/* Tear down the current process because proc_kill flagged it. Does not return.
 * Only legal where the kernel holds no locks: syscall entry/exit, or an IRQ
 * that interrupted ring-3 code. */
void usermode_killed(const char *where);

/* Boot diagnostic: fire `int 0x80` from ring 0 to prove the syscall gate +
 * dispatcher work, independent of any user program. Writes to COM1. */
void usermode_selftest(void);

/* Called BY usermode_asm.asm around the ring-3 transition: arm records the
 * kernel esp to resume at on the current task (and in tss.esp0), and
 * resume_esp reads it back. Not for general use. */
void     usermode_arm_resume(uint32_t esp);
uint32_t usermode_resume_esp(void);

/* Defined in usermode.asm. */
extern void user_mode_enter(uint32_t entry, uint32_t user_stack_top);
extern void user_mode_exit(void);

/* Non-zero while a ring-3 program is running. The exception handler uses this
 * to decide whether a fault kills the program or panics the kernel. */
int  usermode_in_user(void);

/* Kill the running ring-3 program after a fault. Does not return. */
void usermode_fault(uint32_t exc, uint32_t err, uint32_t eip, uint32_t cr2);

/* Details of the most recent ring-3 fault, for the shell to report. */
void usermode_last_fault(uint32_t *exc, uint32_t *err,
                         uint32_t *eip, uint32_t *cr2);

/* Demand paging. A page fault at `cr2` with error `err` is answered with a
 * fresh zeroed page if the address lies in the running process's heap below
 * its break and is simply not mapped yet, whether the touch came from ring 3
 * or from the kernel copying into a user buffer. Returns 0 and the faulting
 * instruction is retried; -1 means the fault is real (or the process is out
 * of pages) and the caller kills or panics as before. */
int usermode_demand_page(uint32_t cr2, uint32_t err);

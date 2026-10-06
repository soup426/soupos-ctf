#pragma once
#include <stdint.h>

/* Load an ELF32 executable from the FAT volume, map its PT_LOAD segments into
 * fresh user pages, and run it in ring 3 until it calls the exit syscall.
 * `args` (may be NULL) is the command-line string the program reads via the
 * args syscall. Returns the program's exit code, or -1 on a load error. */
int exec_elf(const char *path, const char *args);

/* Boot diagnostic: fire `int 0x80` from ring 0 to prove the syscall gate +
 * dispatcher work, independent of any user program. Writes to COM1. */
void usermode_selftest(void);

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

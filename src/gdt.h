#pragma once
#include <stdint.h>

/* Segment selectors (index<<3 | RPL). */
#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UCODE 0x1B   /* index 3, RPL 3 */
#define SEL_UDATA 0x23   /* index 4, RPL 3 */
#define SEL_TSS   0x28   /* index 5 */

void gdt_init(void);

/* Set the ring-0 stack the CPU switches to on a ring3->ring0 transition
 * (interrupt or syscall from user mode).
 *
 * This is PER-TASK state. With more than one ring-3 program alive, each has to
 * trap onto its own kernel stack, or one program's trap frame lands on top of
 * the frame another left there and neither can return. task_yield keeps it in
 * step with `current`; the value itself is task_t.user_resume_esp. */
void tss_set_esp0(uint32_t esp0);

/* Top of the boot ring-0 stack: the esp0 for a task that is not in ring 3. */
uint32_t tss_boot_esp0(void);

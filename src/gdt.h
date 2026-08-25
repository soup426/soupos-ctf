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
 * (interrupt or syscall from user mode). Call before entering user mode. */
void tss_set_esp0(uint32_t esp0);

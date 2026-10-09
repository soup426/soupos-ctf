/* glutton.c - a ring-3 program that never cooperates. Exists to be killed.
 *
 * It makes exactly one syscall (the opening message) and then loops forever on
 * registers alone: no yield, no read, no exit. That matters for testing,
 * because it means the ONLY way back into the kernel is the 100 Hz timer
 * interrupt, so `kill <pid>` on this program can only work through the IRQ
 * unwind path in irq_handler. A program that polls syscalls (whisk.elf) would
 * be killed by the syscall-boundary check instead and prove nothing about the
 * harder case.
 *
 * It is also the proof that preemption works against ring 3: the shell stays
 * responsive while this is running.
 */
#include "ulib.h"

int main(void) {
    eprint("[hog] running, never yields, kill me\n");
    volatile unsigned x = 0;
    for (;;) x++;        /* no syscalls past this point */
}

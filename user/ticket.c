/* ticket.c - proof that two ring-3 programs really run at the same time.
 *
 * Prints "[p:<args>] step N" to the serial port a dozen times, yielding
 * between each, so two copies started with different arguments interleave in
 * the log. One process cannot produce interleaved tags, so a log where
 * [p:A] and [p:B] alternate is the thing itself, not an inference.
 *
 * Verify headless:
 *   cook ticket.elf A &
 *   cook ticket.elf B &
 * then read the serial log (dmesg, or scripts/smoke-test.sh).
 */
#include "ulib.h"

#define STEPS  40      /* ~4 s, so two copies overlap for seconds */
#define GAP    10u      /* ~100 ms between steps, at 100 Hz */

int main(void) {
    char tag[32];
    int n = sys_args(tag, (int)sizeof(tag));
    if (n <= 0) { tag[0] = '?'; tag[1] = '\0'; }

    for (int i = 0; i < STEPS; i++) {
        eprint("[p:"); eprint(tag); eprint("] step "); eprint_int(i); eprint("\n");
        /* Spend the gap yielding rather than spinning, so the other copy and
         * the kernel's own tasks all get the CPU. */
        unsigned until = sys_ticks() + GAP;
        while (sys_ticks() < until) sys_yield();
    }
    print("marker: done\n");
    return 0;
}

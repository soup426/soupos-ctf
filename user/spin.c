/* spin.c - demonstrates cooperative sys_yield() from ring 3.
 *
 * Runs for ~3 seconds of wall-clock time (measured via sys_ticks), and on
 * every iteration calls sys_yield() so background kernel tasks keep running.
 * It logs a "[u] step" marker to the serial port (fd 2) each ~half second.
 *
 * Verify headless: start the `yieldbg` background task in the shell, then
 *   cook spin.elf
 * and watch the serial log - [bg] (kernel task) and [u] (this program)
 * markers interleave, which only happens if SYS_YIELD reaches the scheduler.
 */
#include "ulib.h"

#define RUN_TICKS  300u    /* 100 Hz PIT -> ~3 seconds */
#define STEP_TICKS  50u    /* ~0.5 s between [u] markers */

int main(void) {
    unsigned start = sys_ticks();
    unsigned next  = 0;
    print("spin: yielding for ~3s (see serial for [u]/[bg] interleave)\n");
    for (;;) {
        unsigned elapsed = sys_ticks() - start;
        if (elapsed >= RUN_TICKS) break;
        if (elapsed >= next) {
            eprint("[u] step t="); eprint_int((int)elapsed); eprint("\n");
            next += STEP_TICKS;
        }
        sys_yield();
    }
    print("spin: done\n");
    return 0;
}

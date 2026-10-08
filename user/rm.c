/* rm.c - remove files from ring 3 (v0.48.0).
 *
 *   cook rm.elf PATH [PATH...]
 *
 * One line per path on stderr, "RM removed PATH" or "RM failed PATH",
 * which the console logs, so a test can check each one; the exit code is the
 * number of failures.
 */
#include "ulib.h"

int main(void) {
    char *argv[16];
    int argc = uargv(argv, 16);   /* quoted names may hold spaces (v0.57.2) */
    if (argc <= 0) { eprint("usage: rm.elf PATH [PATH...]\n"); return 1; }
    int failed = 0;
    for (int i = 0; i < argc; i++) {
        const char *p = argv[i];
        if (sys_unlink(p) == 0) { eprint("RM removed "); eprint(p); eprint("\n"); }
        else               { eprint("RM failed "); eprint(p); eprint("\n"); failed++; }
    }
    return failed;
}

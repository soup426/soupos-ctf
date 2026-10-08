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
    char args[256];
    int n = sys_args(args, sizeof(args));
    if (n <= 0) { eprint("usage: rm.elf PATH [PATH...]\n"); return 1; }
    int failed = 0, i = 0;
    while (i < n) {
        while (i < n && args[i] == ' ') i++;
        if (i >= n) break;
        char *p = &args[i];
        while (i < n && args[i] != ' ') i++;
        if (i < n) args[i++] = '\0';
        if (sys_unlink(p) == 0) { eprint("RM removed "); eprint(p); eprint("\n"); }
        else               { eprint("RM failed "); eprint(p); eprint("\n"); failed++; }
    }
    return failed;
}

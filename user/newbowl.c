/* newbowl.c - make bowls from ring 3 (v0.48.0).
 *
 *   cook newbowl.elf PATH [PATH...]
 *
 * One line per path on stderr, "MKDIR made PATH" or "MKDIR failed PATH",
 * which the console logs, so a test can check each one; the exit code is the
 * number of failures.
 */
#include "ulib.h"

int main(void) {
    char *argv[64];                 /* 128 bytes of arguments hold up to 64 (v0.60.0: 16 dropped the 17th) */
    int argc = uargv(argv, 64);   /* quoted names may hold spaces (v0.57.2) */
    if (argc <= 0) { eprint("usage: newbowl.elf PATH [PATH...]\n"); return 1; }
    int failed = 0;
    for (int i = 0; i < argc; i++) {
        const char *p = argv[i];
        if (sys_mkdir(p) == 0) { eprint("MKDIR made "); eprint(p); eprint("\n"); }
        else               { eprint("MKDIR failed "); eprint(p); eprint("\n"); failed++; }
    }
    return failed;
}

/* infuse.c - wait for a file, then try to read another (v0.60.258), a test
 * fixture.
 *
 *   cook infuse.elf GOPATH PATH
 *
 * Polls GOPATH (stat, a tenth of a second apart, a minute at most) until it
 * exists, then opens PATH and prints "INFUSE read: " and its first line, or
 * "INFUSE refused". Started by one cook in the background, it wakes when
 * another, clocked in, makes GOPATH: whose rights its open gets is what
 * clockout-rights-test asks (review R-K2: they were the shell's current
 * cook, not the program's). A file to wait for, not a time (v0.60.260,
 * review R-T19): woken by a timer before a slow login, it was refused by
 * the login screen and passed on the unfixed kernel.
 */
#include "ulib.h"

int main(void) {
    char *tok[3];
    int nt = uargv(tok, 3);
    if (nt != 2) { print("usage: infuse.elf GOPATH PATH\n"); return 2; }
    ustat_t st;
    int k = 0;
    while (sys_stat(tok[0], &st) < 0) {
        if (++k > 600) { print("INFUSE no go\n"); return 3; }
        sys_sleep(100);
    }
    int fd = sys_open(tok[1], 0);
    if (fd < 0) { print("INFUSE refused\n"); return 1; }
    char buf[80];
    int got = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (got < 0) got = 0;
    buf[got] = 0;
    for (int i = 0; i < got; i++) if (buf[i] == '\n') { buf[i] = 0; break; }
    print("INFUSE read: "); print(buf); print("\n");
    return 0;
}

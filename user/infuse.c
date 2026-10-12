/* infuse.c - wait, then try to read a file (v0.60.258), a test fixture.
 *
 *   cook infuse.elf SECONDS PATH
 *
 * Sleeps SECONDS (whole ones, in tenths so a kill ends it soon), then opens
 * PATH and prints "INFUSE read: " and its first line, or "INFUSE refused".
 * Started by one cook in the background, it is still running when another
 * clocks in: whose rights its open gets is what clockout-rights-test asks
 * (review R-K2: they were the shell's current cook, not the program's).
 */
#include "ulib.h"

int main(void) {
    char *tok[3];
    int nt = uargv(tok, 3);
    if (nt != 2) { print("usage: infuse.elf SECONDS PATH\n"); return 2; }
    unsigned secs = 0;
    for (const char *c = tok[0]; *c >= '0' && *c <= '9'; c++) secs = secs * 10 + (unsigned)(*c - '0');
    for (unsigned k = 0; k < secs * 10; k++) sys_sleep(100);
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

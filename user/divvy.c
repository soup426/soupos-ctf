/* divvy.c - stdin to stdout and to files (v0.56.7).
 *
 *   cook call.elf soup | divvy.elf LOG.TXT         to the screen and LOG.TXT
 *   cook call.elf more | divvy.elf -a LOG.TXT      appended
 *   cook spoon.elf X | divvy.elf A.TXT B.TXT | weigh.elf
 *
 * The files are opened through SYS_OPEN, so the cook's permissions apply as
 * to any write. A file that cannot be opened is reported and the rest carry
 * on, as GNU tee does (on stderr, so stdout stays the data); the exit
 * code is then 1.
 */
#include "ulib.h"

int main(void) {
    char *tok[8];
    int nt = uargv(tok, 8);        /* sh's splitting, quotes kept together (v0.57.2) */
    int append = 0, t = 0, status = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++)
        for (char *f = tok[t] + 1; *f; f++) {
            if (*f == 'a') append = 1;
            else { print("usage: divvy.elf [-a] [file...]\n"); return 2; }
        }
    int fds[6], nf = 0;
    for (; t < nt && nf < 6; t++) {
        int fd = sys_open(tok[t], append ? O_APPEND : O_WRITE);
        /* The complaint goes to stderr only, as GNU tee's does: stdout
         * must stay exactly the data, since it is usually a pipe or file. */
        if (fd < 0) { eprint("divvy: cannot open "); eprint(tok[t]); eprint("\n"); status = 1; continue; }
        fds[nf++] = fd;
    }
    char buf[512];
    int got;
    while ((got = sys_read(FD_STDIN, buf, sizeof(buf))) > 0) {
        sys_write(FD_STDOUT, buf, got);
        for (int i = 0; i < nf; i++)
            if (sys_write(fds[i], buf, got) != got) status = 1;
    }
    for (int i = 0; i < nf; i++) sys_close(fds[i]);
    return status;
}

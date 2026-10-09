/* skim.c - the first lines of a file or stdin (v0.56.2).
 *
 *   cook skim.elf RECIPE.TXT         the first 10 lines
 *   cook skim.elf -n 3 RECIPE.TXT    the first 3
 *   cook spoon.elf X | skim.elf -n 1   a pipe stage
 *
 * Bytes are copied as they are: a last line with no newline comes out
 * with none, as GNU head does. It stops reading once it has N lines.
 *
 * As GNU head's too (v0.60.128): -c N the first N bytes; -n -N every line
 * but the last N, -c -N every byte but the last N (these read it all).
 */
#include "ulib.h"

static int to_int(const char *s, int *out) {
    int v = 0, any = 0;
    for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return -1;
    *out = v; return 0;
}

/* Where the last `want` lines of buf[0..len) start (a last line with no
 * newline counts as one), as dregs finds them. */
static int last_lines(const char *buf, int len, int want) {
    int i = len;
    if (i > 0 && buf[i - 1] == '\n') i--;
    int lines = 0;
    while (i > 0) {
        if (buf[i - 1] == '\n' && ++lines == want) break;
        i--;
    }
    return want ? i : len;
}

int main(void) {
    char *tok[6];
    int nt = uargv(tok, 6);        /* sh's splitting, quotes kept together (v0.57.2) */
    int want = 10, t = 0, bytes = 0, but = 0;
    while (t < nt && tok[t][0] == '-' && (tok[t][1] == 'n' || tok[t][1] == 'c') && !tok[t][2]) {
        bytes = tok[t][1] == 'c';
        const char *v = t + 1 < nt ? tok[t + 1] : "";
        but = *v == '-';
        if (*v == '-' || *v == '+') v++;
        if (to_int(v, &want) < 0) { print("usage: skim.elf [-n [-]N | -c [-]N] [file]\n"); return 2; }
        t += 2;
    }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("skim: cannot open "); print(tok[t]); print("\n"); return 1; }
    }
    char buf[512];
    int got;
    if (but) {                                       /* all but the last N: the whole first */
        int cap = 65536, len = 0;
        char *all = malloc((unsigned)cap);
        for (;;) {
            if (len == cap) { char *b = malloc((unsigned)cap * 2); for (int i = 0; i < len; i++) b[i] = all[i]; all = b; cap *= 2; }
            got = sys_read(fd, all + len, cap - len);
            if (got <= 0) break;
            len += got;
        }
        int upto = bytes ? (len > want ? len - want : 0) : last_lines(all, len, want);
        if (upto > 0) sys_write(FD_STDOUT, all, upto);
    } else if (bytes) {
        int left = want;
        while (left > 0 && (got = sys_read(fd, buf, left < (int)sizeof(buf) ? left : (int)sizeof(buf))) > 0) {
            sys_write(FD_STDOUT, buf, got);
            left -= got;
        }
    } else {
        int lines = 0;
        while (lines < want && (got = sys_read(fd, buf, sizeof(buf))) > 0) {
            int upto = got;
            for (int i = 0; i < got; i++)
                if (buf[i] == '\n' && ++lines == want) { upto = i + 1; break; }
            sys_write(FD_STDOUT, buf, upto);
        }
    }
    if (fd != FD_STDIN) sys_close(fd);
    return 0;
}

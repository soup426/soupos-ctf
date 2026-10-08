/* head.c - the first lines of a file or stdin (v0.56.2).
 *
 *   cook head.elf RECIPE.TXT         the first 10 lines
 *   cook head.elf -n 3 RECIPE.TXT    the first 3
 *   cook cat.elf X | head.elf -n 1   a pipe stage
 *
 * Bytes are copied as they are: a last line with no newline comes out
 * with none, as GNU head does. It stops reading once it has N lines.
 */
#include "ulib.h"

static int to_int(const char *s, int *out) {
    int v = 0, any = 0;
    for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return -1;
    *out = v; return 0;
}

int main(void) {
    char *tok[4];
    int nt = uargv(tok, 4);        /* sh's splitting, quotes kept together (v0.57.2) */
    int want = 10, t = 0;
    if (t < nt && tok[t][0] == '-' && tok[t][1] == 'n' && !tok[t][2]) {
        if (t + 1 >= nt || to_int(tok[t + 1], &want) < 0) { print("usage: head.elf [-n N] [file]\n"); return 2; }
        t += 2;
    }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("head: cannot open "); print(tok[t]); print("\n"); return 1; }
    }
    char buf[512];
    int got, lines = 0;
    while (lines < want && (got = sys_read(fd, buf, sizeof(buf))) > 0) {
        int upto = got;
        for (int i = 0; i < got; i++)
            if (buf[i] == '\n' && ++lines == want) { upto = i + 1; break; }
        sys_write(FD_STDOUT, buf, upto);
    }
    if (fd != FD_STDIN) sys_close(fd);
    return 0;
}

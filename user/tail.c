/* tail.c - the last lines of a file or stdin (v0.56.2).
 *
 *   cook tail.elf RECIPE.TXT         the last 10 lines
 *   cook tail.elf -n 3 RECIPE.TXT    the last 3
 *
 * The whole input is read into memory (a pipe cannot be read backwards),
 * then the last N lines are found from the end. A last line with no
 * newline counts as a line and comes out with none, as GNU tail does.
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
        if (t + 1 >= nt || to_int(tok[t + 1], &want) < 0) { print("usage: tail.elf [-n N] [file]\n"); return 2; }
        t += 2;
    }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("tail: cannot open "); print(tok[t]); print("\n"); return 1; }
    }
    /* Read it all: grow by doubling (the bump allocator keeps the old). */
    int cap = 4096, len = 0, got;
    char *all = malloc((unsigned)cap);
    for (;;) {
        if (len == cap) {
            char *bigger = malloc((unsigned)(cap * 2));
            for (int i = 0; i < len; i++) bigger[i] = all[i];
            all = bigger; cap *= 2;
        }
        got = sys_read(fd, all + len, cap - len);
        if (got <= 0) break;
        len += got;
    }
    if (fd != FD_STDIN) sys_close(fd);
    if (want == 0 || len == 0) return 0;
    /* From the end: a final newline ends the last line rather than starting
     * an empty one, so skip it before counting. */
    int i = len;
    if (all[i - 1] == '\n') i--;
    int lines = 0;
    while (i > 0) {
        if (all[i - 1] == '\n' && ++lines == want) break;
        i--;
    }
    sys_write(FD_STDOUT, all + i, len - i);
    return 0;
}

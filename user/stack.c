/* stack.c - lines last first (v0.60.23).
 *
 *   cook stack.elf RECIPE.TXT
 *   spoon.elf RECIPE.TXT | stack.elf
 *
 * As `tac` does it: each line keeps its own newline and the lines come out
 * in the opposite order, so a last line with none ends up joined to the
 * line that follows it in the output (printf 'a\nb' gives "ba\n"), exactly
 * as tac prints. The input is held whole: the first line out is the last
 * one in.
 */
#include "ulib.h"

static char ob[4096];
static int  on;
static void out(const char *p, int n) {
    for (int i = 0; i < n; i++) {
        if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
        ob[on++] = p[i];
    }
}

int main(void) {
    char *tok[4];
    int nt = uargv(tok, 4);
    if (nt > 1) { print("usage: stack.elf [file]\n"); return 2; }
    int fd = FD_STDIN;
    if (nt == 1) {
        fd = sys_open(tok[0], O_READ);
        if (fd < 0) { print("stack: cannot open "); print(tok[0]); print("\n"); return 1; }
    }
    int cap = 65536, len = 0, got;
    char *all = malloc((unsigned)cap);
    for (;;) {
        if (len == cap) {                    /* the bump allocator keeps the old one */
            char *bigger = malloc((unsigned)(cap * 2));
            for (int i = 0; i < len; i++) bigger[i] = all[i];
            all = bigger; cap *= 2;
        }
        got = sys_read(fd, all + len, cap - len);
        if (got <= 0) break;
        len += got;
    }
    if (fd != FD_STDIN) sys_close(fd);
    /* From the end: a line starts after each newline (a final newline ends
     * the last line rather than starting an empty one). */
    int e = len;
    for (int j = len - 2; j >= -1; j--) {
        if (j == -1 || all[j] == '\n') { out(all + j + 1, e - (j + 1)); e = j + 1; }
    }
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

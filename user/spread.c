/* spread.c - tabs out to spaces (v0.60.24).
 *
 *   cook spread.elf RECIPE.TXT         stops every 8 columns
 *   cook spread.elf -t 4 RECIPE.TXT    every 4
 *
 * As `expand` does it under LC_ALL=C: a tab becomes the spaces to the next
 * stop, a backspace steps back a column (and is kept), a newline starts the
 * count again, and every other byte, \r included, is one column.
 */
#include "ulib.h"

static int to_int(const char *s, int *out) {
    int v = 0, any = 0;
    for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return -1;
    *out = v; return 0;
}

static char ob[4096];
static int  on;
static void put(char c) {
    if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
    ob[on++] = c;
}

int main(void) {
    char *tok[4];
    int nt = uargv(tok, 4);
    int stop = 8, t = 0;
    if (t < nt && tok[t][0] == '-' && tok[t][1] == 't') {
        const char *v = tok[t][2] ? tok[t] + 2 : (t + 1 < nt ? tok[++t] : "");
        if (to_int(v, &stop) < 0 || stop < 1) { print("spread: bad tab stop\n"); return 2; }
        t++;
    }
    if (nt - t > 1) { print("usage: spread.elf [-t N] [file]\n"); return 2; }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("spread: cannot open "); print(tok[t]); print("\n"); return 1; }
    }
    char in[4096];
    int col = 0, got;
    while ((got = sys_read(fd, in, sizeof(in))) > 0) {
        for (int i = 0; i < got; i++) {
            char c = in[i];
            if (c == '\t') { do put(' '); while (++col % stop); }
            else if (c == '\b') { put(c); if (col > 0) col--; }
            else if (c == '\n') { put(c); col = 0; }
            else { put(c); col++; }
        }
    }
    if (fd != FD_STDIN) sys_close(fd);
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

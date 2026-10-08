/* knead.c - lines folded to a width (v0.60.15).
 *
 *   cook knead.elf RECIPE.TXT          at 80 columns
 *   cook knead.elf -w 20 RECIPE.TXT    at 20
 *   cook knead.elf -s -w 20 ...        at the last blank that fits
 *
 * As `fold` does it under LC_ALL=C, step for step: a tab moves to the next
 * multiple of eight, a backspace back one, a carriage return to column 0;
 * a line breaks before the character that would pass the width (one that
 * is wider by itself still gets a line of its own), and with -s at the
 * last space or tab, which stays on the first line.
 */
#include "ulib.h"

#define LINE_MAX_BYTES 65536

static int to_int(const char *s, int *out) {
    int v = 0, any = 0;
    for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return -1;
    *out = v; return 0;
}

static int adjust(int column, char c) {
    if (c == '\b') return column > 0 ? column - 1 : 0;
    if (c == '\r') return 0;
    if (c == '\t') return column + 8 - column % 8;
    return column + 1;
}

static char ob[4096];
static int  on;
static void out(const char *p, int n) {
    for (int i = 0; i < n; i++) {
        if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
        ob[on++] = p[i];
    }
}

int main(void) {
    char *tok[8];
    int nt = uargv(tok, 8);
    int width = 80, spaces = 0, t = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++) {
        if (tok[t][1] == 's' && !tok[t][2]) spaces = 1;
        else if (tok[t][1] == 'w') {
            const char *v = tok[t][2] ? tok[t] + 2 : (t + 1 < nt ? tok[++t] : "");
            if (to_int(v, &width) < 0 || width < 1) { print("knead: bad width\n"); return 2; }
        } else { print("usage: knead.elf [-s] [-w N] [file]\n"); return 2; }
    }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("knead: cannot open "); print(tok[t]); print("\n"); return 1; }
    }
    char *line = malloc(LINE_MAX_BYTES);
    int n = 0, column = 0, got;
    char in[4096];
    while ((got = sys_read(fd, in, sizeof(in))) > 0) {
        for (int k = 0; k < got; k++) {
            char c = in[k];
            if (c == '\n') {
                out(line, n); out("\n", 1);
                n = column = 0;
                continue;
            }
            if (n == LINE_MAX_BYTES - 1) { out(line, n); n = 0; }   /* \b and \r can hold a line at no width */
        rescan:
            column = adjust(column, c);
            if (column > width) {
                if (spaces) {
                    int end = n;
                    while (end > 0 && line[end - 1] != ' ' && line[end - 1] != '\t') end--;
                    if (end > 0) {
                        out(line, end); out("\n", 1);
                        for (int i = end; i < n; i++) line[i - end] = line[i];
                        n -= end;
                        column = 0;
                        for (int i = 0; i < n; i++) column = adjust(column, line[i]);
                        goto rescan;
                    }
                }
                if (n == 0) { line[n++] = c; continue; }
                out(line, n); out("\n", 1);
                n = column = 0;
                goto rescan;
            }
            line[n++] = c;
        }
    }
    if (fd != FD_STDIN) sys_close(fd);
    out(line, n);
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

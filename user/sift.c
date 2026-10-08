/* sift.c - lines that contain a fixed string (v0.56.1).
 *
 *   cook sift.elf soup RECIPE.TXT            lines containing "soup"
 *   cook spoon.elf X | sift.elf -v soup        lines that do not
 *   cook sift.elf -c -i soup RECIPE.TXT      how many, ignoring case
 *
 * The pattern is a fixed string, compared byte for byte (ASCII letters
 * folded with -i), like `grep -F`. Lines of any length are matched whole:
 * the line buffer grows as needed. A last line without a newline is still
 * a line, printed with one, as grep does. Exit code 0 if any line was
 * selected, 1 if none, 2 for a usage error or an unreadable file.
 */
#include "ulib.h"

static int  invert, count_only, nocase;
static char *line;
static int  cap, len;

static char fold(char c) { return (nocase && c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int contains(const char *hay, int hn, const char *pat, int pn) {
    if (pn == 0) return 1;
    for (int i = 0; i + pn <= hn; i++) {
        int k = 0;
        while (k < pn && fold(hay[i + k]) == fold(pat[k])) k++;
        if (k == pn) return 1;
    }
    return 0;
}

static void grow(int need) {
    if (need <= cap) return;
    int ncap = cap ? cap : 256;
    while (ncap < need) ncap *= 2;
    char *n = malloc((unsigned)ncap);          /* bump allocator: the old one stays */
    for (int i = 0; i < len; i++) n[i] = line[i];
    line = n; cap = ncap;
}

int main(void) {
    char *tok[8];
    int nt = uargv(tok, 8);        /* sh's splitting, quotes kept together (v0.57.2) */
    int t = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++) {
        for (char *f = tok[t] + 1; *f; f++) {
            if      (*f == 'v') invert = 1;
            else if (*f == 'c') count_only = 1;
            else if (*f == 'i') nocase = 1;
            else { print("sift: unknown flag -"); char c[2] = { *f, 0 }; print(c); print("\n"); return 2; }
        }
    }
    if (t >= nt) { print("usage: sift.elf [-v] [-c] [-i] word [file]\n"); return 2; }
    const char *pat = tok[t++];
    int pn = strlen_(pat);
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("sift: cannot open "); print(tok[t]); print("\n"); return 2; }
    }

    int selected = 0;
    char buf[512];
    int got, eof = 0;
    grow(256);
    while (!eof) {
        got = sys_read(fd, buf, sizeof(buf));
        if (got <= 0) { eof = 1; got = 0; }
        for (int i = 0; i <= got; i++) {
            int end_of_line = (i < got && buf[i] == '\n') || (i == got && eof && len > 0);
            if (i < got && buf[i] != '\n') { grow(len + 1); line[len++] = buf[i]; continue; }
            if (!end_of_line) continue;
            if (contains(line, len, pat, pn) != invert) {
                selected++;
                if (!count_only) { sys_write(FD_STDOUT, line, len); sys_write(FD_STDOUT, "\n", 1); }
            }
            len = 0;
        }
    }
    if (fd != FD_STDIN) sys_close(fd);
    if (count_only) { print_int(selected); print("\n"); }
    return selected ? 0 : 1;
}

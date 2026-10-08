/* sift.c - lines that contain a fixed string (v0.56.1).
 *
 *   cook sift.elf soup RECIPE.TXT            lines containing "soup"
 *   cook spoon.elf X | sift.elf -v soup        lines that do not
 *   cook sift.elf -c -i soup RECIPE.TXT      how many, ignoring case
 *   cook sift.elf -n soup RECIPE.TXT         each with its line number (v0.60.35)
 *   cook sift.elf -o soup RECIPE.TXT         only the matches, one a line
 *
 * The pattern is a fixed string, compared byte for byte (ASCII letters
 * folded with -i), like `grep -F`. Lines of any length are matched whole:
 * the line buffer grows as needed. A last line without a newline is still
 * a line, printed with one, as grep does. Exit code 0 if any line was
 * selected, 1 if none, 2 for a usage error or an unreadable file.
 *
 * -n and -o (v0.60.35) as GNU grep 3.12 does them, measured: -n puts "N:"
 * before each line or match; -o prints each match (non-overlapping, as
 * written in the line when -i folds) on a line of its own. With -v, or an
 * empty pattern, -o prints nothing at all, though the lines still count as
 * selected for the exit code; -c counts lines whatever -o says, and -n does
 * nothing with -c. (Careful measuring this: an interactive shell's grep
 * may be a wrapper around ugrep, which differs on all three.)
 */
#include "ulib.h"

static int  invert, count_only, nocase, numbers, only;
static char *line;
static int  cap, len;

static char fold(char c) { return (nocase && c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* Where pat next occurs in hay at or after from, or -1. */
static int find_at(const char *hay, int hn, const char *pat, int pn, int from) {
    for (int i = from; i + pn <= hn; i++) {
        int k = 0;
        while (k < pn && fold(hay[i + k]) == fold(pat[k])) k++;
        if (k == pn) return i;
    }
    return -1;
}
static int contains(const char *hay, int hn, const char *pat, int pn) {
    if (pn == 0) return 1;
    return find_at(hay, hn, pat, pn, 0) >= 0;
}

static void put_number(int n) {
    char d[12]; int k = 0;
    do { d[k++] = (char)('0' + n % 10); n /= 10; } while (n);
    char o[13]; int m = 0;
    while (k) o[m++] = d[--k];
    o[m++] = ':';
    sys_write(FD_STDOUT, o, m);
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
            else if (*f == 'n') numbers = 1;
            else if (*f == 'o') only = 1;
            else { print("sift: unknown flag -"); char c[2] = { *f, 0 }; print(c); print("\n"); return 2; }
        }
    }
    if (t >= nt) { print("usage: sift.elf [-vcino] word [file]\n"); return 2; }
    const char *pat = tok[t++];
    int pn = strlen_(pat);
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("sift: cannot open "); print(tok[t]); print("\n"); return 2; }
    }

    int selected = 0, lineno = 0;
    int matches = only && !invert && pn > 0;   /* -o in force */
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
            lineno++;
            if (contains(line, len, pat, pn) != invert) {
                selected++;
                if (count_only) { len = 0; continue; }
                if (matches) {
                    for (int at = find_at(line, len, pat, pn, 0); at >= 0; at = find_at(line, len, pat, pn, at + pn)) {
                        if (numbers) put_number(lineno);
                        sys_write(FD_STDOUT, line + at, pn); sys_write(FD_STDOUT, "\n", 1);
                    }
                } else if (!only) {
                    if (numbers) put_number(lineno);
                    sys_write(FD_STDOUT, line, len); sys_write(FD_STDOUT, "\n", 1);
                }
            }
            len = 0;
        }
    }
    if (fd != FD_STDIN) sys_close(fd);
    if (count_only) { print_int(selected); print("\n"); }
    return selected ? 0 : 1;
}

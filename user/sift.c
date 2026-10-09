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
 * -E (v0.60.119): the pattern is a POSIX extended regular expression, as
 * grep -E's, through rx.c; -o then prints each leftmost-longest match.
 *
 * Several FILEs (v0.60.134), as GNU grep: each line (and -o match, and -c
 * count) after NAME: when there is more than one, -H always, -h never; -l
 * the names of the files with a selected line, each once; -w a match
 * that is a whole word (a later one tried when one is not), -x one that
 * is the whole line. A file that will not open is said on stderr and the
 * rest still read; the status is then 2.
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
#define RX_ALLOC(n) malloc(n)
#define RX_FREE(p)  ((void)(p))              /* the bump allocator keeps all */
#include "rx.c"

static int  invert, count_only, nocase, numbers, only, ere, names_only, whole_word, whole_line;
static int  show_name;                 /* NAME: before what is printed */
static const char *fname;
static rx_t rx;                       /* -E (v0.60.119) */
static int  sub[RX_NCAP];
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
static int wordc(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
/* The next match at or after from, as [*ms, *me): honouring -w (both ends
 * at a word's edge; else the search goes on from the next place) and -x
 * (the whole line). 1 found, 0 not. */
static int match_at(const char *hay, int hn, const char *pat, int pn, int from, int *ms, int *me) {
    while (from <= hn) {
        int s0, e0;
        if (ere) {
            if (rx_exec(&rx, hay, hn, from, sub) <= 0) return 0;
            s0 = sub[0]; e0 = sub[1];
        } else {
            int at = pn ? find_at(hay, hn, pat, pn, from) : from;
            if (at < 0) return 0;
            s0 = at; e0 = at + pn;
        }
        if (whole_line && !ere && (s0 != 0 || e0 != hn)) return 0;   /* -E has ^( )$ */
        if (whole_word && ((s0 > 0 && wordc(hay[s0 - 1])) || (e0 < hn && wordc(hay[e0])) || (s0 == e0 && pn))) {
            from = s0 + 1;
            continue;
        }
        *ms = s0; *me = e0;
        return 1;
    }
    return 0;
}
static int contains(const char *hay, int hn, const char *pat, int pn) {
    int a, b;
    return match_at(hay, hn, pat, pn, 0, &a, &b);
}

static void put_number(int n) {
    char d[12]; int k = 0;
    do { d[k++] = (char)('0' + n % 10); n /= 10; } while (n);
    char o[13]; int m = 0;
    while (k) o[m++] = d[--k];
    o[m++] = ':';
    sys_write(FD_STDOUT, o, m);
}
static void put_name(void) {
    if (!show_name) return;
    sys_write(FD_STDOUT, fname, strlen_(fname));
    sys_write(FD_STDOUT, ":", 1);
}

static void grow(int need) {
    if (need <= cap) return;
    int ncap = cap ? cap : 256;
    while (ncap < need) ncap *= 2;
    char *n = malloc((unsigned)ncap);          /* bump allocator: the old one stays */
    for (int i = 0; i < len; i++) n[i] = line[i];
    line = n; cap = ncap;
}

/* One input; how many lines it selected. */
static int sift_fd(int fd, const char *pat, int pn) {
    int selected = 0, lineno = 0;
    int matches = only && !invert && (pn > 0 || ere);   /* -o in force */
    char buf[512];
    int got, eof = 0;
    len = 0;
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
                if (names_only) return selected;           /* -l: one is enough */
                if (count_only) { len = 0; continue; }
                if (matches) {
                    /* each match after the last; an empty one passed over,
                     * not printed, as grep's -o */
                    int ms, me;
                    for (int from = 0; from <= len && match_at(line, len, pat, pn, from, &ms, &me); ) {
                        if (me > ms) {
                            put_name();
                            if (numbers) put_number(lineno);
                            sys_write(FD_STDOUT, line + ms, me - ms); sys_write(FD_STDOUT, "\n", 1);
                            from = me;
                        } else from = ms + 1;
                    }
                } else if (!only) {
                    put_name();
                    if (numbers) put_number(lineno);
                    sys_write(FD_STDOUT, line, len); sys_write(FD_STDOUT, "\n", 1);
                }
            }
            len = 0;
        }
    }
    return selected;
}

int main(void) {
    char *tok[16];
    int nt = uargv(tok, 16);       /* sh's splitting, quotes kept together (v0.57.2) */
    int t = 0, force_name = -1;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++) {
        for (char *f = tok[t] + 1; *f; f++) {
            if      (*f == 'v') invert = 1;
            else if (*f == 'c') count_only = 1;
            else if (*f == 'i') nocase = 1;
            else if (*f == 'n') numbers = 1;
            else if (*f == 'o') only = 1;
            else if (*f == 'E') ere = 1;
            else if (*f == 'l') names_only = 1;
            else if (*f == 'w') whole_word = 1;
            else if (*f == 'x') whole_line = 1;
            else if (*f == 'H') force_name = 1;
            else if (*f == 'h') force_name = 0;
            else { print("sift: unknown flag -"); char c[2] = { *f, 0 }; print(c); print("\n"); return 2; }
        }
    }
    if (t >= nt) { print("usage: sift.elf [-vcinoElwxHh] PATTERN [file...]\n"); return 2; }
    const char *pat = tok[t++];
    int pn = strlen_(pat);
    if (ere) {
        const char *use = pat;
        if (whole_line) {                            /* -x: ^( )$ around it */
            char *w = malloc((unsigned)pn + 5);
            w[0] = '^'; w[1] = '(';
            for (int i = 0; i < pn; i++) w[2 + i] = pat[i];
            w[pn + 2] = ')'; w[pn + 3] = '$'; w[pn + 4] = '\0';
            use = w;
        }
        if (rx_compile(&rx, use, nocase) < 0) { eprint("sift: "); eprint(rx.err); eprint("\n"); return 2; }
    }
    int nfiles = nt - t;
    show_name = force_name >= 0 ? force_name : nfiles > 1;
    int total = 0, trouble = 0;
    for (int f = 0; f < (nfiles ? nfiles : 1); f++) {
        int fd = FD_STDIN;
        fname = "(standard input)";
        if (nfiles) {
            fname = tok[t + f];
            fd = sys_open(fname, O_READ);
            if (fd < 0) { eprint("sift: "); eprint(fname); eprint(": cannot open\n"); trouble = 1; continue; }
        }
        int n = sift_fd(fd, pat, pn);
        if (fd != FD_STDIN) sys_close(fd);
        total += n;
        if (names_only) { if (n) { print(fname); print("\n"); } }
        else if (count_only) { put_name(); print_int(n); print("\n"); }
    }
    return trouble ? 2 : total ? 0 : 1;
}

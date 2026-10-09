/* weigh.c - count lines, words and bytes, in the host's format (v0.56.6).
 *
 *   cook weigh.elf RECIPE.TXT            " 25  50 191 RECIPE.TXT"
 *   cook weigh.elf -l RECIPE.TXT         "25 RECIPE.TXT"
 *   cook call.elf hello | weigh.elf -w   "1"
 *
 * GNU wc's format, measured on the host: one count is printed bare; more
 * than one are each right-aligned to the width of the byte count, one
 * space apart; a file argument's name follows. One difference: GNU uses a
 * width of 7 when its input is a pipe, and weigh.elf cannot tell a pipe from
 * a redirected file (there is no fstat), so it always uses the file rule.
 *
 * Several FILEs (v0.60.135), as GNU wc: a line each and a `total` line,
 * every count padded to the digits of the total bytes (one count or
 * several); -L the longest line (a tab to the next multiple of 8, a
 * control or high byte as nothing), its total the longest of them all.
 * A file that will not open is said, the rest counted, and the status 1.
 *
 * Also emits the counts to serial as a WCOUT marker, so the headless smoke
 * test can read the result of a pipeline rather than infer it.
 */
#include "ulib.h"

static int digits(int v) { int d = 1; while (v >= 10) { v /= 10; d++; } return d; }
static void put_num(int v, int width) {
    char d[12]; int k = 0;
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = k; i < width; i++) sys_write(FD_STDOUT, " ", 1);
    while (k) sys_write(FD_STDOUT, &d[--k], 1);
}

typedef struct { int lines, words, bytes, longest; const char *name; int ok; } count_t;

static void count_fd(int fd, count_t *c) {
    char buf[256];
    int got, in_word = 0, col = 0;
    c->lines = c->words = c->bytes = c->longest = 0;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < got; i++) {
            unsigned char ch = (unsigned char)buf[i];
            c->bytes++;
            if (ch == '\n') { c->lines++; if (col > c->longest) c->longest = col; col = 0; }
            else if (ch == '\t') col = (col / 8 + 1) * 8;
            else if (ch >= 32 && ch < 127) col++;
            if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f') in_word = 0;
            else if (!in_word) { in_word = 1; c->words++; }
        }
    }
    if (col > c->longest) c->longest = col;
}

static int want_l, want_w, want_c, want_L;
static void put_row(const count_t *c, int width, const char *name) {
    int first = 1;
    if (want_l) { put_num(c->lines, width); first = 0; }
    if (want_w) { if (!first) sys_write(FD_STDOUT, " ", 1); put_num(c->words, width); first = 0; }
    if (want_c) { if (!first) sys_write(FD_STDOUT, " ", 1); put_num(c->bytes, width); first = 0; }
    if (want_L) { if (!first) sys_write(FD_STDOUT, " ", 1); put_num(c->longest, width); }
    if (name) { sys_write(FD_STDOUT, " ", 1); print(name); }
    sys_write(FD_STDOUT, "\n", 1);
}

int main(void) {
    char *tok[20];
    int nt = uargv(tok, 20);       /* sh's splitting, quotes kept together (v0.57.2) */
    int t = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++)
        for (char *f = tok[t] + 1; *f; f++) {
            if      (*f == 'l') want_l = 1;
            else if (*f == 'w') want_w = 1;
            else if (*f == 'c') want_c = 1;
            else if (*f == 'L') want_L = 1;
            else { print("usage: weigh.elf [-lwcL] [file...]\n"); return 2; }
        }
    if (!want_l && !want_w && !want_c && !want_L) want_l = want_w = want_c = 1;
    int nfiles = nt - t;
    static count_t c[16];
    count_t total = { 0, 0, 0, 0, "total", 1 };
    int bad = 0, n = nfiles ? (nfiles < 16 ? nfiles : 16) : 1;
    for (int f = 0; f < n; f++) {
        int fd = FD_STDIN;
        c[f].name = nfiles ? tok[t + f] : 0;
        c[f].ok = 1;
        if (nfiles) {
            fd = sys_open(c[f].name, O_READ);
            if (fd < 0) { eprint("weigh: "); eprint(c[f].name); eprint(": cannot open\n"); c[f].ok = 0; bad = 1; continue; }
        }
        count_fd(fd, &c[f]);
        if (fd != FD_STDIN) sys_close(fd);
        total.lines += c[f].lines; total.words += c[f].words; total.bytes += c[f].bytes;
        if (c[f].longest > total.longest) total.longest = c[f].longest;
    }
    int shown = want_l + want_w + want_c + want_L;
    int width = (nfiles > 1 || shown > 1) ? digits(total.bytes) : 0;
    for (int f = 0; f < n; f++) if (c[f].ok) put_row(&c[f], width, c[f].name);
    if (nfiles > 1) put_row(&total, width, "total");

    eprint("WCOUT lines=");  eprint_int(total.lines);
    eprint(" words=");       eprint_int(total.words);
    eprint(" bytes=");       eprint_int(total.bytes);
    eprint("\n");
    return bad;
}

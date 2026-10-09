/* label.c - every line numbered (v0.60.9).
 *
 *   cook label.elf RECIPE.TXT
 *   spoon.elf RECIPE.TXT | label.elf
 *
 * As `nl -ba` numbers them: every line, empty ones included, gets its
 * number right-aligned in six columns and a tab; a number wider than six
 * simply runs wider. A last line with no newline gets one, as nl gives it.
 * nl's logical-page delimiters (lines of \: pairs) are not special here.
 * The input streams through in 4 KB pieces, so any size works.
 *
 * nl's options (v0.60.138): -b a|t|n which lines to number (a, every one,
 * stays this one's default; GNU nl's is t, the non-empty ones), -w WIDTH,
 * -s SEP (a tab), -v START, -i STEP, -n rn|ln|rz (right, left, zeros). A
 * line left unnumbered gets WIDTH and SEP's worth of spaces, as nl's.
 */
#include "ulib.h"

static char ob[4096];
static int  on;

static void put(char c) {
    if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
    ob[on++] = c;
}

static int width = 6, style = 'a', fmt = 'r';      /* fmt: r rn, l ln, z rz */
static const char *sep = "\t";
static long num, step = 1;

static void put_str(const char *x) { while (*x) put(*x++); }
static void put_number(long n) {
    char d[24]; int k = 0, neg = n < 0;
    unsigned long v = neg ? (unsigned long)-n : (unsigned long)n;
    do { d[k++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
    int len = k + neg;
    if (fmt == 'r') for (int pad = len; pad < width; pad++) put(' ');
    if (neg) put('-');
    if (fmt == 'z') for (int pad = len; pad < width; pad++) put('0');
    while (k) put(d[--k]);
    if (fmt == 'l') for (int pad = len; pad < width; pad++) put(' ');
    put_str(sep);
}
static void put_blank(void) {                       /* an unnumbered line's margin */
    for (int i = 0; i < width; i++) put(' ');
    for (const char *x = sep; *x; x++) put(' ');
}
static int to_long(const char *s, long *out) {
    int neg = 0, any = 0; long v = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return -1;
    *out = neg ? -v : v; return 0;
}

int main(void) {
    char *tok[16];
    int nt = uargv(tok, 16), t = 0;
    num = 1;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++) {
        char o = tok[t][1];
        const char *v = tok[t][2] ? tok[t] + 2 : (t + 1 < nt ? tok[++t] : 0);
        if (!v) { print("usage: label.elf [-b a|t|n] [-w N] [-s SEP] [-v N] [-i N] [-n ln|rn|rz] [file]\n"); return 2; }
        long x;
        if (o == 'b' && (v[0] == 'a' || v[0] == 't' || v[0] == 'n') && !v[1]) style = v[0];
        else if (o == 'w' && !to_long(v, &x) && x > 0) width = (int)x;
        else if (o == 's') sep = v;
        else if (o == 'v' && !to_long(v, &x)) num = x;
        else if (o == 'i' && !to_long(v, &x)) step = x;
        else if (o == 'n') {
            if (v[0] == 'l' && v[1] == 'n' && !v[2]) fmt = 'l';
            else if (v[0] == 'r' && v[1] == 'n' && !v[2]) fmt = 'r';
            else if (v[0] == 'r' && v[1] == 'z' && !v[2]) fmt = 'z';
            else { print("label: -n takes ln, rn or rz\n"); return 2; }
        }
        else { print("usage: label.elf [-b a|t|n] [-w N] [-s SEP] [-v N] [-i N] [-n ln|rn|rz] [file]\n"); return 2; }
    }
    if (nt - t > 1) { print("usage: label.elf [options] [file]\n"); return 2; }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("label: cannot open "); print(tok[t]); print("\n"); return 1; }
    }
    char in[4096];
    int at_start = 1, got;
    while ((got = sys_read(fd, in, sizeof(in))) > 0) {
        for (int i = 0; i < got; i++) {
            if (at_start) {
                int empty = in[i] == '\n';
                if (style == 'a' || (style == 't' && !empty)) { put_number(num); num += step; }
                else put_blank();
                at_start = 0;
            }
            put(in[i]);
            if (in[i] == '\n') at_start = 1;
        }
    }
    if (!at_start) put('\n');
    if (fd != FD_STDIN) sys_close(fd);
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

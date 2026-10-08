/* tally.c - numbers in a row (v0.57.7; -s and -w v0.60.36).
 *
 *   cook tally.elf 5            1 to 5
 *   cook tally.elf 3 7          3 to 7
 *   cook tally.elf 10 -3 1      10, 7, 4, 1
 *   cook tally.elf -s , 5       1,2,3,4,5
 *   cook tally.elf -w 8 11      08 09 10 11, one a line
 *   cook tally.elf 3 | rack.elf -r
 *
 * GNU seq's rules for integers: FIRST and STEP default to 1; a run that
 * cannot reach LAST in STEP's direction prints nothing (seq 5 1); STEP 0
 * is an error; arguments like -3 are numbers. One number a line, or SEP
 * between them with -s (taken as written, any length, even empty) and a
 * newline after the last. -w pads with zeros to the width of FIRST or LAST
 * as typed, whichever is wider (a leading + not counted, STEP never; seq -w
 * 007 9 is three wide), after the sign for negatives (-09), as printf's
 * %0*ld does.
 */
#include "ulib.h"

static int parse(const char *s, long *out) {
    int neg = 0; long v = 0; int any = 0;
    if (*s == '-' || *s == '+') { neg = (*s == '-'); s++; }
    for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return -1;
    *out = neg ? -v : v;
    return 0;
}

static char ob[4096];
static int  on;
static void out(const char *p, int n) {
    for (int i = 0; i < n; i++) {
        if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
        ob[on++] = p[i];
    }
}
static void put_long(long v, int width) {
    char d[24]; int k = 0;
    unsigned long u = v < 0 ? (unsigned long)(-v) : (unsigned long)v;
    do { d[k++] = (char)('0' + u % 10); u /= 10; } while (u);
    int len = k + (v < 0);
    if (v < 0) out("-", 1);
    for (; len < width; len++) out("0", 1);
    while (k) { char c = d[--k]; out(&c, 1); }
}
static int typed_width(const char *s) {
    int n = 0;
    if (*s == '+') s++;
    while (s[n]) n++;
    return n;
}

int main(void) {
    char *argv[8];
    int argc = uargv(argv, 8);
    const char *sep = "\n";
    int pad = 0, a = 0;
    /* Options come first; a word like -3 is a number, not an option. */
    while (a < argc && argv[a][0] == '-' && !(argv[a][1] >= '0' && argv[a][1] <= '9')) {
        if (argv[a][1] == 'w' && !argv[a][2]) { pad = 1; a++; continue; }
        if (argv[a][1] == 's') {
            if (argv[a][2]) { sep = argv[a] + 2; a++; continue; }
            if (a + 1 < argc) { sep = argv[a + 1]; a += 2; continue; }
        }
        print("usage: tally.elf [-w] [-s SEP] [FIRST [STEP]] LAST  (integers)\n");
        return 1;
    }
    char **nv = argv + a;
    int nc = argc - a;
    long first = 1, step = 1, last;
    int bad = 0;
    if (nc == 1)      bad = parse(nv[0], &last);
    else if (nc == 2) bad = parse(nv[0], &first) | parse(nv[1], &last);
    else if (nc == 3) bad = parse(nv[0], &first) | parse(nv[1], &step) | parse(nv[2], &last);
    else              bad = -1;
    if (bad) { print("usage: tally.elf [-w] [-s SEP] [FIRST [STEP]] LAST  (integers)\n"); return 1; }
    if (step == 0) { print("tally: the step must not be 0\n"); return 1; }
    int width = 0;
    if (pad) {
        width = typed_width(nv[nc - 1]);
        if (nc >= 2 && typed_width(nv[0]) > width) width = typed_width(nv[0]);
    }
    int sl = 0;
    while (sep[sl]) sl++;
    int any = 0;
    for (long v = first; step > 0 ? v <= last : v >= last; v += step) {
        if (any) out(sep, sl);
        put_long(v, width);
        any = 1;
    }
    if (any) out("\n", 1);
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

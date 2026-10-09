/* tally.c - numbers in a row (v0.57.7; -s and -w v0.60.36; decimals
 * v0.60.148).
 *
 *   cook tally.elf 5            1 to 5
 *   cook tally.elf 3 7          3 to 7
 *   cook tally.elf 10 -3 1      10, 7, 4, 1
 *   cook tally.elf 0 0.5 2      0.0 0.5 1.0 1.5 2.0
 *   cook tally.elf -s , 5       1,2,3,4,5
 *   cook tally.elf -w 8 11      08 09 10 11, one a line
 *   cook tally.elf 3 | rack.elf -r
 *
 * GNU seq's rules: FIRST and STEP default to 1; a run that cannot reach
 * LAST in STEP's direction prints nothing (seq 5 1); STEP 0 is an error;
 * arguments like -3 and -.5 are numbers. One number a line, or SEP
 * between them with -s (taken as written, any length, even empty) and a
 * newline after the last.
 *
 * Decimals (v0.60.148): user programs have no floating point, so every
 * number is a 64-bit integer in units of the finest fraction typed. Each
 * value is FIRST plus a whole number of STEPs, exactly, and stops past
 * LAST; it is printed with as many decimals as FIRST or STEP has (LAST's
 * do not count: seq 1 1.50 prints 1), as seq's %.Nf does. 17 digits at
 * most a number, counting the decimals the finest one gives it. -w pads with zeros after the sign to the width of FIRST
 * or LAST as typed (a leading + not counted, STEP never; seq -w 007 9 is
 * three wide), each widened or narrowed to the printed decimals, the
 * arithmetic GNU's seq does on the typed strings.
 */
#include "ulib.h"

#define USAGE "usage: tally.elf [-w] [-s SEP] [FIRST [STEP]] LAST  (decimals allowed)\n"

/* A number as typed: its value in units of 10^-prec, its decimals, and
 * the width seq gives it (as typed, a + not counted, "1." one narrower,
 * ".5" one wider for the 0 seq prints). */
typedef struct { long long v; int prec, width; } num_t;
static int parse(const char *s, num_t *n) {
    if (*s == '+') s++;
    const char *p = s, *dot = 0;
    int neg = 0, digits = 0, prec = 0;
    long long v = 0;
    if (*p == '-') { neg = 1; p++; }
    for (; *p; p++) {
        if (*p == '.' && !dot) { dot = p; continue; }
        if (*p < '0' || *p > '9') return -1;
        if (++digits > 18) return -1;
        v = v * 10 + (*p - '0');
        if (dot) prec++;
    }
    if (!digits) return -1;
    n->v = neg ? -v : v;
    n->prec = prec;
    n->width = 0;
    while (s[n->width]) n->width++;
    if (dot && !prec) n->width--;
    else if (dot && (dot == s || dot[-1] < '0' || dot[-1] > '9')) n->width++;
    return 0;
}
static long long pow10(int k) { long long r = 1; while (k-- > 0) r *= 10; return r; }

static char ob[4096];
static int  on;
static void out(const char *p, int n) {
    for (int i = 0; i < n; i++) {
        if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
        ob[on++] = p[i];
    }
}
/* v in units of 10^-prec, as printf's %0*.*f prints it. */
static void put_fixed(long long v, int prec, int width) {
    char d[48]; int k = 0;
    unsigned long long u = v < 0 ? (unsigned long long)(-v) : (unsigned long long)v;
    for (int i = 0; i < prec; i++) { d[k++] = (char)('0' + u % 10); u /= 10; }
    if (prec) d[k++] = '.';
    do { d[k++] = (char)('0' + u % 10); u /= 10; } while (u);
    int len = k + (v < 0);
    if (v < 0) out("-", 1);
    for (; len < width; len++) out("0", 1);
    while (k) { char c = d[--k]; out(&c, 1); }
}

int main(void) {
    char *argv[8];
    int argc = uargv(argv, 8);
    const char *sep = "\n";
    int pad = 0, a = 0;
    /* Options come first; a word like -3 is a number, not an option. */
    while (a < argc && argv[a][0] == '-' && !((argv[a][1] >= '0' && argv[a][1] <= '9') || argv[a][1] == '.')) {
        if (argv[a][1] == 'w' && !argv[a][2]) { pad = 1; a++; continue; }
        if (argv[a][1] == 's') {
            if (argv[a][2]) { sep = argv[a] + 2; a++; continue; }
            if (a + 1 < argc) { sep = argv[a + 1]; a += 2; continue; }
        }
        print(USAGE);
        return 1;
    }
    char **nv = argv + a;
    int nc = argc - a;
    num_t first = { 1, 0, 1 }, step = { 1, 0, 1 }, last;
    int bad = 0;
    if (nc == 1)      bad = parse(nv[0], &last);
    else if (nc == 2) bad = parse(nv[0], &first) | parse(nv[1], &last);
    else if (nc == 3) bad = parse(nv[0], &first) | parse(nv[1], &step) | parse(nv[2], &last);
    else              bad = -1;
    if (bad) { print(USAGE); return 1; }
    if (step.v == 0) { print("tally: the step must not be 0\n"); return 1; }
    int P = first.prec > step.prec ? first.prec : step.prec;       /* printed decimals */
    int S = P > last.prec ? P : last.prec;                          /* worked in */
    long long F = first.v * pow10(S - first.prec), T = step.v * pow10(S - step.prec), L = last.v * pow10(S - last.prec);
    for (int i = 0; i < 3; i++) {                                   /* 17 digits once scaled: v + T cannot overflow */
        const num_t *n = i == 0 ? &first : i == 1 ? &step : &last;
        long long m = n->v < 0 ? -n->v : n->v;
        int nd = 0;
        do { nd++; m /= 10; } while (m);
        if (nd + S - n->prec > 17) { print("tally: too many digits\n"); return 1; }
    }
    int width = 0;
    if (pad) {                                                      /* seq's arithmetic, as typed */
        int fw = first.width + (P - first.prec) + (first.prec == 0 && P ? 1 : 0);
        int lw = last.width + (P - last.prec);
        if (last.prec && !P) lw--;
        if (!last.prec && P) lw++;
        width = fw > lw ? fw : lw;
    }
    long long down = pow10(S - P);
    int sl = 0;
    while (sep[sl]) sl++;
    int any = 0;
    for (long long v = F; T > 0 ? v <= L : v >= L; v += T) {
        if (any) out(sep, sl);
        put_fixed(v / down, P, width);
        any = 1;
    }
    if (any) out("\n", 1);
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

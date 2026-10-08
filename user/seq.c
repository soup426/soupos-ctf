/* seq.c - numbers in a row (v0.57.7).
 *
 *   cook seq.elf 5            1 to 5
 *   cook seq.elf 3 7          3 to 7
 *   cook seq.elf 10 -3 1      10, 7, 4, 1
 *   cook seq.elf 3 | sort.elf -r
 *
 * GNU seq's rules for integers: FIRST and STEP default to 1; a run that
 * cannot reach LAST in STEP's direction prints nothing (seq 5 1); STEP 0
 * is an error; arguments like -3 are numbers. One number a line.
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
static void put_long(long v) {
    char d[24]; int k = 0;
    unsigned long u = v < 0 ? (unsigned long)(-v) : (unsigned long)v;
    do { d[k++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) d[k++] = '-';
    char out[26]; int n = 0;
    while (k) out[n++] = d[--k];
    out[n++] = '\n';
    sys_write(FD_STDOUT, out, n);
}

int main(void) {
    char *argv[4];
    int argc = uargv(argv, 4);
    long first = 1, step = 1, last;
    int bad = 0;
    if (argc == 1)      bad = parse(argv[0], &last);
    else if (argc == 2) bad = parse(argv[0], &first) | parse(argv[1], &last);
    else if (argc == 3) bad = parse(argv[0], &first) | parse(argv[1], &step) | parse(argv[2], &last);
    else                bad = -1;
    if (bad) { print("usage: seq.elf [FIRST [STEP]] LAST  (integers)\n"); return 1; }
    if (step == 0) { print("seq: the step must not be 0\n"); return 1; }
    if (step > 0) for (long v = first; v <= last; v += step) put_long(v);
    else          for (long v = first; v >= last; v += step) put_long(v);
    return 0;
}

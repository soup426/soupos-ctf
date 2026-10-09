/* dish.c - printf, under a kitchen name (v0.60.49).
 *
 *   cook dish.elf "%s is %d\n" soup 5
 *   cook dish.elf "%-6s|%05d\n" a 42
 *
 * As the host's printf (GNU coreutils) does it: %s %d %i %x %o %c and %%,
 * the - and 0 flags, a width, a precision (the most of a %s, the fewest
 * digits of a number), and the escapes \n \t \r \a \\ \" and \c (no more output)
 * in the format.
 * The format is used again while arguments remain, if it took any; an
 * argument that is not there is "" for %s and %c and 0 for a number. The
 * numbers are 32-bit here: a negative %x or %o is not GNU's 64-bit one.
 */
#include "ulib.h"

static char ob[4096];
static int  on;
static void put(char c) {
    if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
    ob[on++] = c;
}
static void pad(int n, char c) { while (n-- > 0) put(c); }

static long to_long(const char *s) {
    int neg = 0; long v = 0;
    if (*s == '-' || *s == '+') { neg = (*s == '-'); s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

static void put_text(const char *s, int left, int width, int prec) {
    int len = 0;
    while (s[len] && (prec < 0 || len < prec)) len++;
    if (!left) pad(width - len, ' ');
    for (int i = 0; i < len; i++) put(s[i]);
    if (left) pad(width - len, ' ');
}

static void put_num(long v, int base, int left, int zero, int width, int prec) {
    char d[40]; int k = 0;
    int neg = (base == 10 && v < 0);
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    do { d[k++] = "0123456789abcdef"[u % (unsigned)base]; u /= (unsigned)base; } while (u);
    if (prec == 0 && v == 0) k = 0;                /* %.0d of 0 prints nothing */
    int digits = k > prec ? k : prec;
    int len = digits + neg;
    if (!left && !(zero && prec < 0)) pad(width - len, ' ');
    if (neg) put('-');
    if (!left && zero && prec < 0) pad(width - len, '0');
    pad(digits - k, '0');
    while (k) put(d[--k]);
    if (left) pad(width - len, ' ');
}

int main(void) {
    char *argv[48];
    int argc = uargv(argv, 48);
    if (argc < 1) { print("usage: dish.elf FORMAT [ARGS...]\n"); return 1; }
    const char *fmt = argv[0];
    int ai = 1;
    for (;;) {
        int took = 0;
        for (const char *p = fmt; *p; p++) {
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                case 'n': put('\n'); break;
                case 't': put('\t'); break;
                case 'r': put('\r'); break;
                case 'a': put('\a'); break;
                case '\\': put('\\'); break;
                case '"': put('"'); break;
                case 'c': if (on) sys_write(FD_STDOUT, ob, on); return 0;   /* \c: nothing more */
                default: put('\\'); put(*p); break;
                }
                continue;
            }
            if (*p != '%') { put(*p); continue; }
            p++;
            if (*p == '%') { put('%'); continue; }
            int left = 0, zero = 0, width = 0, prec = -1;
            for (; *p == '-' || *p == '0'; p++) { if (*p == '-') left = 1; else zero = 1; }
            while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
            if (*p == '.') { prec = 0; p++; while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0'); }
            const char *arg = ai < argc ? argv[ai++] : 0;
            took = 1;
            switch (*p) {
            case 's': put_text(arg ? arg : "", left, width, prec); break;
            case 'c': { char c[2] = { arg ? arg[0] : 0, 0 }; put_text(c, left, width, -1); break; }
            case 'd': case 'i': put_num(arg ? to_long(arg) : 0, 10, left, zero, width, prec); break;
            case 'x': put_num(arg ? to_long(arg) : 0, 16, left, zero, width, prec); break;
            case 'o': put_num(arg ? to_long(arg) : 0, 8, left, zero, width, prec); break;
            case 0: p--; put('%'); break;
            default: put('%'); put(*p); ai--; took = 0; break;
            }
        }
        if (!took || ai >= argc) break;
    }
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

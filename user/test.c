/* test.c - evaluate a condition; exit 0 if true, 1 if false (v0.58.1).
 *
 *   cook test.elf -f /RECIPE.TXT && pour /RECIPE.TXT
 *   cook test.elf $N -gt 3 || cook echo.elf small
 *   cook test.elf ! -d /home/x && mkbowl /home/x
 *
 * As test does: one argument is true if it is not empty; ! in front
 * negates; -e -f -d FILE; -z -n STRING; S1 = S2, S1 != S2; and -eq -ne -lt
 * -le -gt -ge on integers. Exit 2 for a usage error or a bad integer.
 * Programs have no stat call, so -f is "opens for reading and is not a
 * bowl" and -d is "can be listed": a file the cook may not read counts as
 * absent for -f and -e, where test would say it exists. -a and -o are not
 * supported (use && and ||).
 */
#include "ulib.h"

static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int is_dir(const char *p)  { udirent_t e; return sys_readdir(p, 0, &e) >= 0; }
static int is_file(const char *p) {
    if (is_dir(p)) return 0;
    int fd = sys_open(p, O_READ);
    if (fd < 0) return 0;
    sys_close(fd);
    return 1;
}
static int to_int(const char *s, long *out) {
    int neg = 0, any = 0; long v = 0;
    while (*s == ' ') s++;
    if (*s == '-' || *s == '+') { neg = (*s == '-'); s++; }
    for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return -1;
    *out = neg ? -v : v; return 0;
}

/* 1 true, 0 false, -1 usage error */
static int eval(char **a, int n) {
    if (n == 0) return 0;
    if (n >= 1 && streq(a[0], "!") && n > 1) { int r = eval(a + 1, n - 1); return r < 0 ? r : !r; }
    if (n == 1) return a[0][0] != '\0';
    if (n == 2) {
        if (streq(a[0], "-z")) return a[1][0] == '\0';
        if (streq(a[0], "-n")) return a[1][0] != '\0';
        if (streq(a[0], "-e")) return is_file(a[1]) || is_dir(a[1]);
        if (streq(a[0], "-f")) return is_file(a[1]);
        if (streq(a[0], "-d")) return is_dir(a[1]);
        return -1;
    }
    if (n == 3) {
        if (streq(a[1], "="))  return streq(a[0], a[2]);
        if (streq(a[1], "!=")) return !streq(a[0], a[2]);
        long x, y;
        const char *op = a[1];
        int known = streq(op, "-eq") || streq(op, "-ne") || streq(op, "-lt") || streq(op, "-le") || streq(op, "-gt") || streq(op, "-ge");
        if (!known) return -1;
        if (to_int(a[0], &x) < 0 || to_int(a[2], &y) < 0) return -1;
        if (streq(op, "-eq")) return x == y;
        if (streq(op, "-ne")) return x != y;
        if (streq(op, "-lt")) return x <  y;
        if (streq(op, "-le")) return x <= y;
        if (streq(op, "-gt")) return x >  y;
        return x >= y;
    }
    return -1;
}

int main(void) {
    char *argv[8];
    int argc = uargv(argv, 8);
    int r = eval(argv, argc);
    if (r < 0) { eprint("test: usage error\n"); return 2; }
    return r ? 0 : 1;
}

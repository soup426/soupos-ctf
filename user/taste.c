/* taste.c - evaluate a condition; exit 0 if true, 1 if false (v0.58.1).
 *
 *   cook taste.elf -f /RECIPE.TXT && pour /RECIPE.TXT
 *   cook taste.elf $N -gt 3 || cook call.elf small
 *   cook taste.elf ! -d /home/x && mkbowl /home/x
 *
 * As test does: one argument is true if it is not empty; ! in front
 * negates; -e -f -d FILE; -z -n STRING; S1 = S2, S1 != S2; and -eq -ne -lt
 * -le -gt -ge on integers. Exit 2 for a usage error or a bad integer.
 * -e -f -d ask SYS_STAT (v0.59.0), so a file the cook may not read still
 * exists, as with test; a path behind a bowl the cook may not search does
 * not. -a and -o are not supported (use && and ||).
 */
#include "ulib.h"

static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int is_dir(const char *p)  { ustat_t st; return sys_stat(p, &st) == 0 && st.is_dir; }
static int is_file(const char *p) { ustat_t st; return sys_stat(p, &st) == 0 && !st.is_dir; }
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
    if (r < 0) { eprint("taste: usage error\n"); return 2; }
    return r ? 0 : 1;
}

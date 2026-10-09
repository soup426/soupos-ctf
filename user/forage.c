/* forage.c - every name under a path (v0.59.2).
 *
 *   cook forage.elf /home                     everything under /home
 *   cook forage.elf / -name '*.TXT'           by name (quote the pattern,
 *                                           or the shell globs it first)
 *   cook forage.elf /etc -type f              files only (d: bowls only)
 *
 * The starting path first, then everything under it, one per line, as
 * find prints. -name matches the last component with * and ?; -type f|d.
 * A bowl that cannot be listed is reported on stderr and makes the exit
 * code 1, as find does; the rest carries on.
 */
#include "ulib.h"

static const char *pat;
static int want_type, status;     /* want_type: 0 any, 'f', 'd' */

static int match(const char *p, const char *n) {
    if (!*p) return !*n;
    if (*p == '*') { for (;; n++) { if (match(p + 1, n)) return 1; if (!*n) return 0; } }
    if (!*n) return 0;
    return (*p == '?' || *p == *n) && match(p + 1, n + 1);
}
static const char *base(const char *path) {
    const char *b = path;
    for (const char *c = path; *c; c++) if (*c == '/' && c[1]) b = c + 1;
    return b;
}
static void show(const char *path, int is_dir) {
    if (want_type && want_type != (is_dir ? 'd' : 'f')) return;
    if (pat && !match(pat, base(path))) return;
    print(path); print("\n");
}

static void walk(const char *dir, int depth) {
    if (depth > 24) return;
    udirent_t e;
    int i = 0, r;
    while ((r = sys_readdir(dir, i, &e)) == 1) {
        char child[200]; int k = 0;
        for (const char *c = dir; *c && k < 190; c++) child[k++] = *c;
        if (k == 0 || child[k - 1] != '/') child[k++] = '/';
        for (const char *c = e.name; *c && k < 199; c++) child[k++] = *c;
        child[k] = 0;
        show(child, (int)e.is_dir);
        if (e.is_dir) walk(child, depth + 1);
        i++;
    }
    if (r < 0) { eprint("forage: cannot list "); eprint(dir); eprint("\n"); status = 1; }
}

int main(void) {
    char *argv[8];
    int argc = uargv(argv, 8), a = 0;
    const char *start = ".";
    if (a < argc && argv[a][0] != '-') start = argv[a++];
    for (; a < argc; a++) {
        if (argv[a][0] == '-' && argv[a][1] == 'n' && a + 1 < argc) pat = argv[++a];
        else if (argv[a][0] == '-' && argv[a][1] == 't' && a + 1 < argc && (argv[a + 1][0] == 'f' || argv[a + 1][0] == 'd')) want_type = argv[++a][0];
        else { print("usage: forage.elf [path] [-name PATTERN] [-type f|d]\n"); return 1; }
    }
    ustat_t st;
    if (sys_stat(start, &st) < 0) { eprint("forage: no such path\n"); return 1; }
    show(start, (int)st.is_dir);
    if (st.is_dir) walk(start, 0);
    return status;
}

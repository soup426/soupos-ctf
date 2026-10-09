/* dregs.c - the last lines of a file or stdin (v0.56.2).
 *
 *   cook dregs.elf RECIPE.TXT         the last 10 lines
 *   cook dregs.elf -n 3 RECIPE.TXT    the last 3
 *
 * A pipe cannot be read backwards, so the input is read forwards and only
 * what could still be among the last N lines is kept (v0.60.11): when the
 * buffer fills, everything before the start of the last N lines is dropped
 * and the rest moved down. It grows only when N lines take most of it, so
 * 14 MB through a pipe needs no more than the lines it prints (it used to
 * keep it all, and that much filled swap). A last line with no newline
 * counts as a line and comes out with none, as GNU tail does.
 *
 * As GNU tail's too (v0.60.128): -c N the last N bytes; -n +N from line N
 * on, -c +N from byte N on (these read forwards and print as they go).
 */
#include "ulib.h"

static int to_int(const char *s, int *out) {
    int v = 0, any = 0;
    for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return -1;
    *out = v; return 0;
}

/* Where the last `want` lines of buf[0..len) start. A final newline ends the
 * last line rather than starting an empty one, so it is skipped first; an
 * unfinished last line counts as one, which keeps a line too many at worst. */
static int last_lines(const char *buf, int len, int want) {
    int i = len;
    if (i > 0 && buf[i - 1] == '\n') i--;
    int lines = 0;
    while (i > 0) {
        if (buf[i - 1] == '\n' && ++lines == want) break;
        i--;
    }
    return i;
}

int main(void) {
    char *tok[6];
    int nt = uargv(tok, 6);        /* sh's splitting, quotes kept together (v0.57.2) */
    int want = 10, t = 0, bytes = 0, from = 0;
    while (t < nt && tok[t][0] == '-' && (tok[t][1] == 'n' || tok[t][1] == 'c') && !tok[t][2]) {
        bytes = tok[t][1] == 'c';
        const char *v = t + 1 < nt ? tok[t + 1] : "";
        from = *v == '+';
        if (*v == '-' || *v == '+') v++;
        if (to_int(v, &want) < 0) { print("usage: dregs.elf [-n [+]N | -c [+]N] [file]\n"); return 2; }
        t += 2;
    }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("dregs: cannot open "); print(tok[t]); print("\n"); return 1; }
    }
    if (from) {                                      /* +N: skip to it, then copy on */
        char b[512];
        int g, skip = want > 0 ? want - 1 : 0;       /* +0 is +1, as GNU's */
        while ((g = sys_read(fd, b, sizeof(b))) > 0) {
            int i = 0;
            if (bytes) { int k = skip < g ? skip : g; skip -= k; i = k; }
            else while (skip > 0 && i < g) { if (b[i++] == '\n') skip--; }
            if (i < g) sys_write(FD_STDOUT, b + i, g - i);
        }
        if (fd != FD_STDIN) sys_close(fd);
        return 0;
    }
    int cap = 65536, len = 0, got;
    char *buf = malloc((unsigned)cap);
    for (;;) {
        if (len == cap) {
            int cut = bytes ? len - want : want ? last_lines(buf, len, want) : len;
            if (cut < 0) cut = 0;
            if (cut >= cap / 2) {
                for (int i = cut; i < len; i++) buf[i - cut] = buf[i];
                len -= cut;
            } else {
                /* The last N lines fill most of the buffer by themselves:
                 * grow, rather than move nearly all of it for every small
                 * read (the bump allocator keeps the old one, so only then). */
                char *bigger = malloc((unsigned)(cap * 2));
                for (int i = 0; i < len; i++) bigger[i] = buf[i];
                buf = bigger; cap *= 2;
            }
        }
        got = sys_read(fd, buf + len, cap - len);
        if (got <= 0) break;
        len += got;
    }
    if (fd != FD_STDIN) sys_close(fd);
    if (want == 0 || len == 0) return 0;
    int i = bytes ? (len > want ? len - want : 0) : last_lines(buf, len, want);
    sys_write(FD_STDOUT, buf + i, len - i);
    return 0;
}

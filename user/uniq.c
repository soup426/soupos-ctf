/* uniq.c - adjacent duplicate lines folded into one (v0.56.5).
 *
 *   cook sort.elf X | uniq.elf        each distinct line once
 *   cook uniq.elf -c X                with how many times, as "%7d line"
 *   cook uniq.elf -d X                only the lines that repeat
 *
 * Only ADJACENT lines are compared, as uniq does (sort first for all
 * duplicates). A last line with no newline is still a line, printed with
 * one. Lines of any length: the two line buffers grow.
 */
#include "ulib.h"

static int counts, dups_only;

typedef struct { char *s; int n, cap; } buf_t;
static void put(buf_t *b, char c) {
    if (b->n == b->cap) {
        int nc = b->cap ? b->cap * 2 : 256;
        char *ns = malloc((unsigned)nc);
        for (int i = 0; i < b->n; i++) ns[i] = b->s[i];
        b->s = ns; b->cap = nc;
    }
    b->s[b->n++] = c;
}
static int same(const buf_t *a, const buf_t *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) if (a->s[i] != b->s[i]) return 0;
    return 1;
}
static void emit(const buf_t *b, int count) {
    if (dups_only && count < 2) return;
    if (counts) {                                   /* "%7d " */
        char d[12]; int k = 0, v = count;
        do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        for (int i = k; i < 7; i++) sys_write(FD_STDOUT, " ", 1);
        while (k) sys_write(FD_STDOUT, &d[--k], 1);
        sys_write(FD_STDOUT, " ", 1);
    }
    sys_write(FD_STDOUT, b->s, b->n);
    sys_write(FD_STDOUT, "\n", 1);
}

int main(void) {
    char args[96];
    int an = sys_args(args, sizeof(args));
    if (an < 0) an = 0;
    args[an < (int)sizeof(args) ? an : (int)sizeof(args) - 1] = '\0';
    char *tok[6]; int nt = 0;
    for (char *p = args; *p && nt < 6; ) {
        while (*p == ' ') *p++ = '\0';
        if (!*p) break;
        tok[nt++] = p;
        while (*p && *p != ' ') p++;
    }
    int t = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++)
        for (char *f = tok[t] + 1; *f; f++) {
            if      (*f == 'c') counts = 1;
            else if (*f == 'd') dups_only = 1;
            else { print("usage: uniq.elf [-c] [-d] [file]\n"); return 2; }
        }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("uniq: cannot open "); print(tok[t]); print("\n"); return 1; }
    }
    buf_t prev = {0, 0, 0}, cur = {0, 0, 0};
    int have_prev = 0, count = 0, got, partial = 0;
    char buf[512];
    for (;;) {
        got = sys_read(fd, buf, sizeof(buf));
        int last = got <= 0;
        for (int i = 0; i < (last ? 1 : got); i++) {
            int eol;
            if (last) { if (!partial) break; eol = 1; }
            else if (buf[i] != '\n') { put(&cur, buf[i]); partial = 1; continue; }
            else eol = 1;
            if (eol) {
                if (have_prev && same(&prev, &cur)) count++;
                else {
                    if (have_prev) emit(&prev, count);
                    buf_t tmp = prev; prev = cur; cur = tmp;
                    have_prev = 1; count = 1;
                }
                cur.n = 0; partial = 0;
            }
        }
        if (last) break;
    }
    if (have_prev) emit(&prev, count);
    if (fd != FD_STDIN) sys_close(fd);
    return 0;
}

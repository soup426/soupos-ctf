/* sort.c - lines in order (v0.56.3).
 *
 *   cook sort.elf NAMES.TXT          byte order, like LC_ALL=C sort
 *   cook sort.elf -r NAMES.TXT       reversed
 *   cook sort.elf -n NUMS.TXT        by the number at the start of the line
 *   cook cat.elf X | sort.elf -n -r  a pipe stage
 *
 * The rules are GNU sort's in the C locale: bytes compare unsigned and a
 * prefix sorts first; -n reads leading blanks, an optional '-', digits and
 * an optional '.' fraction (a line with no number is 0, and -0 is 0), and
 * lines -n calls equal fall back to the byte comparison of the whole line;
 * -r reverses all of it. A last line with no newline is printed with one.
 * The input is read whole into memory, then a merge sort over pointers.
 */
#include "ulib.h"

typedef struct { const char *s; int n; } line_t;
static int numeric, reverse;

static int bytecmp(const line_t *a, const line_t *b) {
    int m = a->n < b->n ? a->n : b->n;
    for (int i = 0; i < m; i++) {
        unsigned char x = (unsigned char)a->s[i], y = (unsigned char)b->s[i];
        if (x != y) return x < y ? -1 : 1;
    }
    return a->n < b->n ? -1 : a->n > b->n ? 1 : 0;
}

/* The number at the start of a line, as pieces: sign, integer digits
 * without leading zeros, fraction digits without trailing zeros. */
typedef struct { int neg; const char *ip; int il; const char *fp; int fl; } num_t;
static num_t parse(const line_t *l) {
    num_t r = {0, 0, 0, 0, 0};
    int i = 0;
    while (i < l->n && (l->s[i] == ' ' || l->s[i] == '\t')) i++;
    if (i < l->n && l->s[i] == '-') { r.neg = 1; i++; }
    while (i < l->n && l->s[i] == '0') i++;
    r.ip = l->s + i;
    while (i < l->n && l->s[i] >= '0' && l->s[i] <= '9') { i++; r.il++; }
    if (i < l->n && l->s[i] == '.') {
        i++; r.fp = l->s + i;
        while (i < l->n && l->s[i] >= '0' && l->s[i] <= '9') { i++; r.fl++; }
        while (r.fl > 0 && r.fp[r.fl - 1] == '0') r.fl--;
    }
    if (r.il == 0 && r.fl == 0) r.neg = 0;              /* -0 and no number are 0 */
    return r;
}

static int numcmp(const line_t *a, const line_t *b) {
    num_t x = parse(a), y = parse(b);
    if (x.neg != y.neg) return x.neg ? -1 : 1;
    int sign = x.neg ? -1 : 1, c = 0;
    if (x.il != y.il) c = x.il < y.il ? -1 : 1;
    for (int i = 0; !c && i < x.il; i++) if (x.ip[i] != y.ip[i]) c = x.ip[i] < y.ip[i] ? -1 : 1;
    for (int i = 0; !c && (i < x.fl || i < y.fl); i++) {
        char p = i < x.fl ? x.fp[i] : '0', q = i < y.fl ? y.fp[i] : '0';
        if (p != q) c = p < q ? -1 : 1;
    }
    return c * sign;
}

static int cmp(const line_t *a, const line_t *b) {
    int c = numeric ? numcmp(a, b) : 0;
    if (!c) c = bytecmp(a, b);                        /* the last resort */
    return reverse ? -c : c;
}

static void msort(line_t *v, line_t *tmp, int n) {
    if (n < 2) return;
    int h = n / 2;
    msort(v, tmp, h); msort(v + h, tmp, n - h);
    int i = 0, j = h, k = 0;
    while (i < h && j < n) tmp[k++] = cmp(&v[j], &v[i]) < 0 ? v[j++] : v[i++];
    while (i < h) tmp[k++] = v[i++];
    while (j < n) tmp[k++] = v[j++];
    for (k = 0; k < n; k++) v[k] = tmp[k];
}

int main(void) {
    char *tok[6];
    int nt = uargv(tok, 6);        /* sh's splitting, quotes kept together (v0.57.2) */
    int t = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++)
        for (char *f = tok[t] + 1; *f; f++) {
            if      (*f == 'r') reverse = 1;
            else if (*f == 'n') numeric = 1;
            else { print("usage: sort.elf [-r] [-n] [file]\n"); return 2; }
        }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("sort: cannot open "); print(tok[t]); print("\n"); return 2; }
    }
    int cap = 4096, len = 0, got;
    char *all = malloc((unsigned)cap);
    for (;;) {
        if (len == cap) {
            char *bigger = malloc((unsigned)(cap * 2));
            if (!bigger) { print("sort: out of memory\n"); return 2; }
            for (int i = 0; i < len; i++) bigger[i] = all[i];
            all = bigger; cap *= 2;
        }
        got = sys_read(fd, all + len, cap - len);
        if (got <= 0) break;
        len += got;
    }
    if (fd != FD_STDIN) sys_close(fd);
    int nl = 0;
    for (int i = 0; i < len; i++) if (all[i] == '\n') nl++;
    if (len && all[len - 1] != '\n') nl++;
    line_t *v = malloc((unsigned)(nl ? nl : 1) * sizeof(line_t));
    line_t *tmp = malloc((unsigned)(nl ? nl : 1) * sizeof(line_t));
    if (!v || !tmp) { print("sort: out of memory\n"); return 2; }
    int k = 0, s = 0;
    for (int i = 0; i < len; i++)
        if (all[i] == '\n') { v[k].s = all + s; v[k].n = i - s; k++; s = i + 1; }
    if (s < len) { v[k].s = all + s; v[k].n = len - s; k++; }
    msort(v, tmp, k);
    for (int i = 0; i < k; i++) { sys_write(FD_STDOUT, v[i].s, v[i].n); sys_write(FD_STDOUT, "\n", 1); }
    return 0;
}

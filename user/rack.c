/* rack.c - lines in order (v0.56.3).
 *
 *   cook rack.elf NAMES.TXT          byte order, like LC_ALL=C sort
 *   cook rack.elf -r NAMES.TXT       reversed
 *   cook rack.elf -n NUMS.TXT        by the number at the start of the line
 *   cook spoon.elf X | rack.elf -n -r  a pipe stage
 *   cook rack.elf -t , -k 2,2 -n X     by the second comma field, as a number
 *   cook rack.elf -u X                 one of each (v0.60.25)
 *
 * The rules are GNU sort's in the C locale: bytes compare unsigned and a
 * prefix sorts first; -n reads leading blanks, an optional '-', digits and
 * an optional '.' fraction (a line with no number is 0, and -0 is 0), and
 * lines -n calls equal fall back to the byte comparison of the whole line;
 * -r reverses all of it. A last line with no newline is printed with one.
 * The input is read whole into memory, then a merge sort over pointers.
 *
 * -k N[,M] (v0.60.25) compares from the start of field N to the end of
 * field M (the end of the line without M). Fields are split on the -t
 * character, or else where a blank follows a non-blank, so a field starts
 * with the blanks before it, as GNU sort counts them (no -b here). A line
 * short of fields has an empty key. -u keeps the first line of each run the
 * keys call equal, with the whole-line comparison switched off for it, as
 * GNU sort does; the merge sort is stable, so "first" is the input's first.
 *
 * -f (v0.60.133) folds lower case to upper for comparing keys; -b skips
 * the blanks at the start of each key (the whole line's, without -k). The
 * last-resort comparison of whole lines is bytes as they are, as GNU's.
 */
#include "ulib.h"

typedef struct { const char *s; int n; } line_t;
static int numeric, reverse, unique, fold, blanks;
static int kbeg, kend;             /* -k fields, 1-based; 0: the whole line / to the end */
static char tsep;                  /* -t character, or 0 for blanks */

static int blank(char c) { return c == ' ' || c == '\t'; }

/* The key of a line: from the start of field kbeg to the end of field kend. */
static line_t key_of(const line_t *l) {
    if (!kbeg) return *l;
    const char *p = l->s, *e = l->s + l->n;
    for (int f = 1; f < kbeg && p < e; f++) {             /* past kbeg-1 fields */
        if (tsep) { while (p < e && *p != tsep) p++; if (p < e) p++; }
        else      { while (p < e && blank(*p)) p++; while (p < e && !blank(*p)) p++; }
    }
    const char *q = e;
    if (kend) {
        q = l->s;
        for (int f = 1; f < kend && q < e; f++) {
            if (tsep) { while (q < e && *q != tsep) q++; if (q < e) q++; }
            else      { while (q < e && blank(*q)) q++; while (q < e && !blank(*q)) q++; }
        }
        if (tsep) { while (q < e && *q != tsep) q++; }
        else      { while (q < e && blank(*q)) q++; while (q < e && !blank(*q)) q++; }
    }
    line_t k = { p, q > p ? (int)(q - p) : 0 };
    return k;
}

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

static int foldcmp(const line_t *a, const line_t *b) {
    int m = a->n < b->n ? a->n : b->n;
    for (int i = 0; i < m; i++) {
        unsigned char x = (unsigned char)a->s[i], y = (unsigned char)b->s[i];
        if (x >= 'a' && x <= 'z') x = (unsigned char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (unsigned char)(y - 32);
        if (x != y) return x < y ? -1 : 1;
    }
    return a->n < b->n ? -1 : a->n > b->n ? 1 : 0;
}
static void skip_blanks(line_t *k) { while (k->n > 0 && blank(*k->s)) { k->s++; k->n--; } }

/* The keys, then (unless -u) the whole line as the last resort. */
static int keycmp(const line_t *a, const line_t *b) {
    line_t x = key_of(a), y = key_of(b);
    if (blanks) { skip_blanks(&x); skip_blanks(&y); }
    int c = numeric ? numcmp(&x, &y) : fold ? foldcmp(&x, &y) : bytecmp(&x, &y);
    return reverse ? -c : c;
}
static int cmp(const line_t *a, const line_t *b) {
    int c = keycmp(a, b);
    if (!c && !unique) { c = bytecmp(a, b); if (reverse) c = -c; }
    return c;
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
    char *tok[12];
    int nt = uargv(tok, 12);       /* sh's splitting, quotes kept together (v0.57.2) */
    int t = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++)
        for (char *f = tok[t] + 1; *f; f++) {
            if      (*f == 'r') reverse = 1;
            else if (*f == 'n') numeric = 1;
            else if (*f == 'u') unique = 1;
            else if (*f == 'f') fold = 1;
            else if (*f == 'b') blanks = 1;
            else if (*f == 't' || *f == 'k') {
                /* the value is the rest of this word, or the next word */
                const char *v = f[1] ? f + 1 : (t + 1 < nt ? tok[++t] : 0);
                if (!v || !*v) { print("usage: rack.elf [-rnufb] [-t C] [-k N[,M]] [file]\n"); return 2; }
                if (*f == 't') {
                    if (v[1]) { print("rack: -t takes one character\n"); return 2; }
                    tsep = v[0];
                } else {
                    kbeg = 0; kend = 0;
                    while (*v >= '0' && *v <= '9') kbeg = kbeg * 10 + (*v++ - '0');
                    if (*v == ',') { v++; while (*v >= '0' && *v <= '9') kend = kend * 10 + (*v++ - '0'); }
                    if (kbeg < 1 || *v || (kend && kend < kbeg)) { print("rack: bad -k\n"); return 2; }
                }
                break;                       /* the word is used up */
            }
            else { print("usage: rack.elf [-rnufb] [-t C] [-k N[,M]] [file]\n"); return 2; }
        }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("rack: cannot open "); print(tok[t]); print("\n"); return 2; }
    }
    int cap = 4096, len = 0, got;
    char *all = malloc((unsigned)cap);
    for (;;) {
        if (len == cap) {
            char *bigger = malloc((unsigned)(cap * 2));
            if (!bigger) { print("rack: out of memory\n"); return 2; }
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
    if (!v || !tmp) { print("rack: out of memory\n"); return 2; }
    int k = 0, s = 0;
    for (int i = 0; i < len; i++)
        if (all[i] == '\n') { v[k].s = all + s; v[k].n = i - s; k++; s = i + 1; }
    if (s < len) { v[k].s = all + s; v[k].n = len - s; k++; }
    msort(v, tmp, k);
    for (int i = 0; i < k; i++) {
        if (unique && i > 0 && keycmp(&v[i - 1], &v[i]) == 0) continue;   /* -u: the first of a run */
        sys_write(FD_STDOUT, v[i].s, v[i].n); sys_write(FD_STDOUT, "\n", 1);
    }
    return 0;
}

/* spot.c - what changed between two files, diff under a kitchen name
 * (v0.60.122): spot the difference.
 *
 *   cook spot.elf OLD NEW
 *
 * GNU diff's normal format: for each change, `2c2` (`3,4d2`, `5a6,7`),
 * the old lines with "< ", a `---` between, the new ones with "> ", and
 * "\ No newline at end of file" after a last line without one. Lines
 * the two share at the start and end are set aside first, as GNU's are;
 * the rest is a longest common subsequence, where two are as short
 * taking the new line first (what GNU's diff gives, measured). Status 0 the same, 1 different, 2 trouble.
 * Each file's part that differs: 2000 lines at most.
 */
#include "ulib.h"

typedef struct { char *text; int len; int *at; int n; int noeol; } file_t;

static int load(const char *name, file_t *f) {
    int fd = sys_open(name, O_READ);
    if (fd < 0) return -1;
    int cap = 4096, got;
    f->text = malloc((unsigned)cap); f->len = 0;
    for (;;) {
        if (f->len == cap) {
            char *b = malloc((unsigned)cap * 2);
            for (int i = 0; i < f->len; i++) b[i] = f->text[i];
            f->text = b; cap *= 2;
        }
        got = sys_read(fd, f->text + f->len, cap - f->len);
        if (got <= 0) break;
        f->len += got;
    }
    sys_close(fd);
    f->noeol = f->len && f->text[f->len - 1] != '\n';
    f->n = 0;
    for (int i = 0; i < f->len; i++) if (f->text[i] == '\n') f->n++;
    if (f->noeol) f->n++;
    f->at = malloc((unsigned)(f->n + 1) * sizeof(int));
    int k = 0; f->at[0] = 0;
    for (int i = 0; i < f->len; i++) if (f->text[i] == '\n') f->at[++k] = i + 1;
    f->at[f->n] = f->len + (f->noeol ? 1 : 0);          /* as if it had one */
    return 0;
}
static int llen(const file_t *f, int i) { return f->at[i + 1] - f->at[i] - 1; }
/* lines i of a and j of b the same: bytes, and a missing last newline too */
static int same(const file_t *a, int i, const file_t *b, int j) {
    int n = llen(a, i);
    if (n != llen(b, j)) return 0;
    if ((a->noeol && i == a->n - 1) != (b->noeol && j == b->n - 1)) return 0;
    const char *p = a->text + a->at[i], *q = b->text + b->at[j];
    for (int k = 0; k < n; k++) if (p[k] != q[k]) return 0;
    return 1;
}

static char out[1024]; static int on;
static void flush(void) { if (on) sys_write(FD_STDOUT, out, on); on = 0; }
static void put(const char *s, int n) { for (int i = 0; i < n; i++) { if (on == (int)sizeof(out)) flush(); out[on++] = s[i]; } }
static void puts_(const char *s) { put(s, strlen_(s)); }
static void putn(int v) { char d[12]; int k = 0; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) put(&d[--k], 1); }
static void range(int lo, int hi) { putn(lo); if (hi > lo) { put(",", 1); putn(hi); } }
static void line(const file_t *f, int i, const char *mark) {
    puts_(mark);
    put(f->text + f->at[i], llen(f, i));
    put("\n", 1);
    if (f->noeol && i == f->n - 1) puts_("\\ No newline at end of file\n");
}

/* one change: a's lines [i0,i1) went, b's [j0,j1) came (0-based) */
static void hunk(const file_t *a, int i0, int i1, const file_t *b, int j0, int j1) {
    if (i1 > i0 && j1 > j0) { range(i0 + 1, i1); put("c", 1); range(j0 + 1, j1); }
    else if (i1 > i0)       { range(i0 + 1, i1); put("d", 1); putn(j0); }
    else                    { putn(i0); put("a", 1); range(j0 + 1, j1); }
    put("\n", 1);
    for (int i = i0; i < i1; i++) line(a, i, "< ");
    if (i1 > i0 && j1 > j0) puts_("---\n");
    for (int j = j0; j < j1; j++) line(b, j, "> ");
}


/* ---- GNU diff's choice ------------------------------------------------- */

static int *xv, *yv;                     /* the lines left in, as equivalence classes */
static int *xreal, *yreal;               /* where each came from */
static char *cha, *chb;                  /* changed, with a 0 on each side (index -1 and n) */
static int *fd, *bd;                     /* diagonals, offset so -(m+1)..n+1 are in */

static void diag(int xoff, int xlim, int yoff, int ylim, int *xmid, int *ymid) {
    int dmin = xoff - ylim, dmax = xlim - yoff;
    int fmid = xoff - yoff, bmid = xlim - ylim;
    int fmin = fmid, fmax = fmid, bmin = bmid, bmax = bmid;
    int odd = (fmid - bmid) & 1;
    fd[fmid] = xoff;
    bd[bmid] = xlim;
    for (;;) {
        int d;
        if (fmin > dmin) fd[--fmin - 1] = -1; else ++fmin;
        if (fmax < dmax) fd[++fmax + 1] = -1; else --fmax;
        for (d = fmax; d >= fmin; d -= 2) {
            int x, y, tlo = fd[d - 1], thi = fd[d + 1];
            int x0 = tlo < thi ? thi : tlo + 1;
            for (x = x0, y = x0 - d; x < xlim && y < ylim && xv[x] == yv[y]; x++, y++) ;
            fd[d] = x;
            if (odd && bmin <= d && d <= bmax && bd[d] <= x) { *xmid = x; *ymid = y; return; }
        }
        if (bmin > dmin) bd[--bmin - 1] = 0x7fffffff; else ++bmin;
        if (bmax < dmax) bd[++bmax + 1] = 0x7fffffff; else --bmax;
        for (d = bmax; d >= bmin; d -= 2) {
            int x, y, tlo = bd[d - 1], thi = bd[d + 1];
            int x0 = tlo < thi ? tlo : thi - 1;
            for (x = x0, y = x0 - d; xoff < x && yoff < y && xv[x - 1] == yv[y - 1]; x--, y--) ;
            bd[d] = x;
            if (!odd && fmin <= d && d <= fmax && x <= fd[d]) { *xmid = x; *ymid = y; return; }
        }
    }
}

static void compareseq(int xoff, int xlim, int yoff, int ylim) {
    while (xoff < xlim && yoff < ylim && xv[xoff] == yv[yoff]) { xoff++; yoff++; }
    while (xoff < xlim && yoff < ylim && xv[xlim - 1] == yv[ylim - 1]) { xlim--; ylim--; }
    if (xoff == xlim) while (yoff < ylim) chb[yreal[yoff++]] = 1;
    else if (yoff == ylim) while (xoff < xlim) cha[xreal[xoff++]] = 1;
    else {
        int xmid, ymid;
        diag(xoff, xlim, yoff, ylim, &xmid, &ymid);
        compareseq(xoff, xmid, yoff, ymid);
        compareseq(xmid, xlim, ymid, ylim);
    }
}

/* GNU's discard_confusing_lines: discards[i] 1 for a line with no match
 * in the other file, 2 (provisional) for one with very many; provisional
 * ones stay only inside a run of discards, under the same rules. */
static void discard(int end, const int *eq, const int *other_count, char *disc) {
    int many = 5, tem = end / 64;
    while ((tem = tem >> 2) > 0) many *= 2;
    for (int i = 0; i < end; i++) {
        int nmatch = other_count[eq[i]];
        disc[i] = nmatch == 0 ? 1 : nmatch > many ? 2 : 0;
    }
    for (int i = 0; i < end; i++) {
        if (disc[i] == 2) disc[i] = 0;
        else if (disc[i]) {
            int j, length, provisional = 0;
            for (j = i; j < end; j++) { if (!disc[j]) break; if (disc[j] == 2) ++provisional; }
            while (j > i && disc[j - 1] == 2) disc[--j] = 0, --provisional;
            length = j - i;
            if (provisional * 4 > length) {
                while (j > i) if (disc[--j] == 2) disc[j] = 0;
            } else {
                int consec, minimum = 1, t = length >> 2;
                while (0 < (t >>= 2)) minimum <<= 1;
                minimum++;
                for (j = 0, consec = 0; j < length; j++)
                    if (disc[i + j] != 2) consec = 0;
                    else if (minimum == ++consec) j -= consec;
                    else if (minimum < consec) disc[i + j] = 0;
                for (j = 0, consec = 0; j < length; j++) {
                    if (j >= 8 && disc[i + j] == 1) break;
                    if (disc[i + j] == 2) consec = 0, disc[i + j] = 0;
                    else if (disc[i + j] == 0) consec = 0;
                    else consec++;
                    if (consec == 3) break;
                }
                i += length - 1;
                for (j = 0, consec = 0; j < length; j++) {
                    if (j >= 8 && disc[i - j] == 1) break;
                    if (disc[i - j] == 2) consec = 0, disc[i - j] = 0;
                    else if (disc[i - j] == 0) consec = 0;
                    else consec++;
                    if (consec == 3) break;
                }
            }
        }
    }
}

/* GNU's shift_boundaries, for one file against the other. */
static void shift(char *changed, const char *other_changed, const int *equivs, int i_end) {
    int i = 0, j = 0;
    for (;;) {
        int runlength, start, corresponding;
        while (i < i_end && !changed[i]) { while (other_changed[j++]) ; i++; }
        if (i == i_end) break;
        start = i;
        while (changed[++i]) ;
        while (other_changed[j]) j++;
        do {
            runlength = i - start;
            while (start && equivs[start - 1] == equivs[i - 1]) {
                changed[--start] = 1;
                changed[--i] = 0;
                while (changed[start - 1]) start--;
                while (other_changed[--j]) ;
            }
            corresponding = other_changed[j - 1] ? i : i_end;
            while (i != i_end && equivs[start] == equivs[i]) {
                changed[start++] = 0;
                changed[i++] = 1;
                while (changed[i]) i++;
                while (other_changed[++j]) corresponding = i;
            }
        } while (runlength != i - start);
        while (corresponding < i) {
            changed[--start] = 1;
            changed[--i] = 0;
            while (other_changed[--j]) ;
        }
    }
}

int main(void) {
    char *tok[4];
    int nt = uargv(tok, 4);
    if (nt != 2) { eprint("usage: spot.elf OLD NEW\n"); return 2; }
    static file_t A, B;
    if (load(tok[0], &A) < 0) { eprint("spot.elf: "); eprint(tok[0]); eprint(": cannot open\n"); return 2; }
    if (load(tok[1], &B) < 0) { eprint("spot.elf: "); eprint(tok[1]); eprint(": cannot open\n"); return 2; }
    int lo = 0, ha = A.n, hb = B.n;
    while (lo < ha && lo < hb && same(&A, lo, &B, lo)) lo++;
    while (ha > lo && hb > lo && same(&A, ha - 1, &B, hb - 1)) { ha--; hb--; }
    int n = ha - lo, m = hb - lo;
    if (!n && !m) return 0;
    if (n > 2000 || m > 2000) { eprint("spot.elf: over 2000 lines differ in a file\n"); return 2; }
    /* equivalence classes: the same text, the same number (1..) */
    int *ea = malloc((unsigned)(n + 1) * sizeof(int)), *eb = malloc((unsigned)(m + 1) * sizeof(int));
    int *rep_f = malloc((unsigned)(n + m + 1) * sizeof(int)), *rep_i = malloc((unsigned)(n + m + 1) * sizeof(int));
    int ncls = 0;
    for (int k = 0; k < n + m; k++) {
        const file_t *f = k < n ? &A : &B; int li = lo + (k < n ? k : k - n);
        int c = 0;
        for (int q = 1; q <= ncls && !c; q++) if (same(rep_f[q] ? &B : &A, rep_i[q], f, li)) c = q;
        if (!c) { c = ++ncls; rep_f[c] = k >= n; rep_i[c] = li; }
        if (k < n) ea[k] = c; else eb[k - n] = c;
    }
    int *cnt_a = malloc((unsigned)(ncls + 1) * sizeof(int)), *cnt_b = malloc((unsigned)(ncls + 1) * sizeof(int));
    for (int q = 0; q <= ncls; q++) cnt_a[q] = cnt_b[q] = 0;
    for (int i = 0; i < n; i++) cnt_a[ea[i]]++;
    for (int j = 0; j < m; j++) cnt_b[eb[j]]++;
    char *da = malloc((unsigned)n + 1), *db = malloc((unsigned)m + 1);
    discard(n, ea, cnt_b, da);
    discard(m, eb, cnt_a, db);
    char *ca0 = malloc((unsigned)n + 2), *cb0 = malloc((unsigned)m + 2);
    for (int i = 0; i < n + 2; i++) ca0[i] = 0;
    for (int j = 0; j < m + 2; j++) cb0[j] = 0;
    cha = ca0 + 1; chb = cb0 + 1;                 /* cha[-1] and cha[n] are 0 */
    xv = malloc((unsigned)(n + 1) * sizeof(int)); xreal = malloc((unsigned)(n + 1) * sizeof(int));
    yv = malloc((unsigned)(m + 1) * sizeof(int)); yreal = malloc((unsigned)(m + 1) * sizeof(int));
    int nx = 0, ny = 0;
    for (int i = 0; i < n; i++) if (!da[i]) { xv[nx] = ea[i]; xreal[nx++] = i; } else cha[i] = 1;
    for (int j = 0; j < m; j++) if (!db[j]) { yv[ny] = eb[j]; yreal[ny++] = j; } else chb[j] = 1;
    int diags = nx + ny + 3;
    int *dbuf = malloc((unsigned)(2 * diags) * sizeof(int));
    fd = dbuf + ny + 1; bd = fd + diags;
    compareseq(0, nx, 0, ny);
    shift(cha, chb, ea, n);
    shift(chb, cha, eb, m);
    int i = 0, j = 0;
    while (i < n || j < m) {
        if (cha[i] || chb[j]) {
            int i0 = i, j0 = j;
            while (cha[i]) i++;
            while (chb[j]) j++;
            hunk(&A, lo + i0, lo + i, &B, lo + j0, lo + j);
        }
        i++; j++;
    }
    flush();
    return 1;
}

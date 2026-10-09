/* rx.c - POSIX extended regular expressions, small (v0.60.119).
 *
 * One file, included by the programs that match (sift.elf -E, reckon.elf)
 * and by the kernel's shell ([[ =~ ]]); the includer defines RX_ALLOC(n)
 * and RX_FREE(p) first. No libc.
 *
 * What it reads: literals; . ; [...] with ranges, [^...], a ] first, and
 * [:alpha:] [:digit:] [:alnum:] [:upper:] [:lower:] [:space:] [:punct:]
 * [:xdigit:] [:blank:]; * + ? and {m}, {m,}, {m,n}; ^ $ anywhere; | and
 * ( ) (nine groups saved); \w \W \s \S, \b \B \< \>; \x for any other x.
 * As GNU's ERE: a * + or ? at the start of a pattern, a group or an
 * alternative repeats nothing and so is passed over (GNU warns of it), a { that does not open a valid count is a literal {, and
 * a ) with no ( is a literal ).
 *
 * How: the pattern is compiled to a small program (char, any, class,
 * split, jump, save, and the assertions) and run as a Pike VM: every
 * thread in step, one per instruction, so the time is the text's length
 * times the program's and no pattern can make it hang. The answer is
 * POSIX's leftmost-longest match, the one grep -o prints; the groups are
 * those of the first thread to reach that end. The thread lists are walked
 * with a stack of their own, not by recursion, for the kernel's sake.
 */

#ifndef RX_ALLOC
#error "define RX_ALLOC and RX_FREE before including rx.c"
#endif

#define RX_MAXINST 512
#define RX_MAXGRP  9                    /* ( ) groups saved, BASH_REMATCH[1..9] */
#define RX_MAXCLS  32

enum { RXI_CHAR, RXI_ANY, RXI_CLASS, RXI_SPLIT, RXI_JMP, RXI_SAVE, RXI_BOL, RXI_EOL, RXI_WORD, RXI_MATCH };
enum { RXW_B, RXW_NB, RXW_LT, RXW_GT };            /* \b \B \< \> */

typedef struct { unsigned char op, c; short x, y; } rx_inst_t;

typedef struct {
    rx_inst_t prog[RX_MAXINST];
    int n;
    unsigned char cls[RX_MAXCLS][32];
    int ncls;
    int ngrp;                           /* ( ) in the pattern, up to RX_MAXGRP saved */
    int icase;
    const char *err;
    /* while compiling */
    const char *pat;
    int plen;
    short gidx[512];                    /* the group number of the ( at each place */
    char copy[1024];                    /* the pattern, a stray ) escaped */
} rx_t;

static int rx_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int rx_upper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int rx_isword(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

static int rx_emit(rx_t *r, int op, int c, int x, int y) {
    if (r->n >= RX_MAXINST) { if (!r->err) r->err = "pattern too large"; return r->n - 1; }
    rx_inst_t *in = &r->prog[r->n];
    in->op = (unsigned char)op; in->c = (unsigned char)c; in->x = (short)x; in->y = (short)y;
    return r->n++;
}

static void rx_set(unsigned char *s, int c) { s[(c >> 3) & 31] |= (unsigned char)(1 << (c & 7)); }
static int  rx_has(const unsigned char *s, int c) { return (s[(c >> 3) & 31] >> (c & 7)) & 1; }

static int rx_eq(const char *a, const char *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }

static void rx_named(unsigned char *s, const char *nm, int nl) {
    for (int c = 0; c < 256; c++) {
        int in = 0;
        if (nl == 5 && rx_eq(nm, "alpha", 5)) in = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        else if (nl == 5 && rx_eq(nm, "digit", 5)) in = c >= '0' && c <= '9';
        else if (nl == 5 && rx_eq(nm, "alnum", 5)) in = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        else if (nl == 5 && rx_eq(nm, "upper", 5)) in = c >= 'A' && c <= 'Z';
        else if (nl == 5 && rx_eq(nm, "lower", 5)) in = c >= 'a' && c <= 'z';
        else if (nl == 5 && rx_eq(nm, "space", 5)) in = c == ' ' || (c >= 9 && c <= 13);
        else if (nl == 5 && rx_eq(nm, "blank", 5)) in = c == ' ' || c == '\t';
        else if (nl == 5 && rx_eq(nm, "punct", 5)) in = c > 32 && c < 127 && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
        else if (nl == 6 && rx_eq(nm, "xdigit", 6)) in = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (in) rx_set(s, c);
    }
}

/* Where the [...] starting at p ends (just past its ]), or -1. */
static int rx_class_end(const rx_t *r, int p) {
    int i = p + 1;
    if (i < r->plen && r->pat[i] == '^') i++;
    if (i < r->plen && r->pat[i] == ']') i++;
    while (i < r->plen && r->pat[i] != ']') {
        if (r->pat[i] == '[' && i + 1 < r->plen && r->pat[i + 1] == ':') {
            int j = i + 2;
            while (j + 1 < r->plen && !(r->pat[j] == ':' && r->pat[j + 1] == ']')) j++;
            if (j + 1 < r->plen) { i = j + 2; continue; }
        }
        i++;
    }
    return i < r->plen ? i + 1 : -1;
}

static int rx_new_class(rx_t *r) {
    if (r->ncls >= RX_MAXCLS) { if (!r->err) r->err = "too many [ ] in the pattern"; return 0; }
    unsigned char *s = r->cls[r->ncls];
    for (int i = 0; i < 32; i++) s[i] = 0;
    return r->ncls++;
}

static void rx_class(rx_t *r, int p, int e) {        /* [ at p, its ] at e - 1 */
    int k = rx_new_class(r);
    unsigned char *s = r->cls[k];
    int i = p + 1, neg = 0;
    if (r->pat[i] == '^') { neg = 1; i++; }
    int first = 1;
    while (i < e - 1) {
        int c = (unsigned char)r->pat[i];
        if (c == ']' && !first) break;
        first = 0;
        if (c == '[' && i + 1 < e - 1 && r->pat[i + 1] == ':') {
            int j = i + 2;
            while (j + 1 < e && !(r->pat[j] == ':' && r->pat[j + 1] == ']')) j++;
            rx_named(s, r->pat + i + 2, j - (i + 2));
            i = j + 2;
            continue;
        }
        if (i + 2 < e - 1 && r->pat[i + 1] == '-' && r->pat[i + 2] != ']') {
            int hi = (unsigned char)r->pat[i + 2];
            for (int x = c; x <= hi; x++) rx_set(s, x);
            i += 3;
            continue;
        }
        rx_set(s, c);
        i++;
    }
    if (r->icase) for (int c = 0; c < 256; c++) if (rx_has(s, c)) { rx_set(s, rx_lower(c)); rx_set(s, rx_upper(c)); }
    if (neg) { for (int b = 0; b < 32; b++) s[b] = (unsigned char)~s[b]; s['\n' >> 3] &= (unsigned char)~(1 << ('\n' & 7)); }
    rx_emit(r, RXI_CLASS, k, 0, 0);
}

static void rx_escape_class(rx_t *r, int c) {        /* \w \W \s \S */
    int k = rx_new_class(r);
    unsigned char *s = r->cls[k];
    for (int x = 0; x < 256; x++) {
        int in = (c == 'w' || c == 'W') ? rx_isword(x) : (x == ' ' || (x >= 9 && x <= 13));
        if (c == 'W' || c == 'S') in = !in && x != '\n';
        if (in) rx_set(s, x);
    }
    rx_emit(r, RXI_CLASS, k, 0, 0);
}

static void rx_alt(rx_t *r, int lo, int hi);

/* The atom at p: where it ends, or -1. */
static int rx_atom_end(rx_t *r, int p, int hi) {
    char c = r->pat[p];
    if (c == '(') {
        int d = 0;
        for (int i = p; i < hi; i++) {
            if (r->pat[i] == '\\') { i++; continue; }
            if (r->pat[i] == '[') { int e = rx_class_end(r, i); if (e < 0) return -1; i = e - 1; continue; }
            if (r->pat[i] == '(') d++;
            else if (r->pat[i] == ')' && --d == 0) return i + 1;
        }
        if (!r->err) r->err = "Unmatched ( or \\(";
        return -1;
    }
    if (c == '[') {
        int e = rx_class_end(r, p);
        if (e < 0 || e > hi) { if (!r->err) r->err = "Unmatched [, [^, [:, [., or [="; return -1; }
        return e;
    }
    if (c == '\\') return p + 2 <= hi ? p + 2 : p + 1;
    return p + 1;
}

static void rx_gen_atom(rx_t *r, int p, int e) {
    char c = r->pat[p];
    if (c == '(') {
        int g = r->gidx[p];
        if (g <= RX_MAXGRP) rx_emit(r, RXI_SAVE, 0, 2 * g, 0);
        rx_alt(r, p + 1, e - 1);
        if (g <= RX_MAXGRP) rx_emit(r, RXI_SAVE, 0, 2 * g + 1, 0);
    } else if (c == '[') rx_class(r, p, e);
    else if (c == '.') rx_emit(r, RXI_ANY, 0, 0, 0);
    else if (c == '^') rx_emit(r, RXI_BOL, 0, 0, 0);
    else if (c == '$') rx_emit(r, RXI_EOL, 0, 0, 0);
    else if (c == '\\' && e == p + 2) {
        char x = r->pat[p + 1];
        if (x == 'w' || x == 'W' || x == 's' || x == 'S') rx_escape_class(r, x);
        else if (x == 'b') rx_emit(r, RXI_WORD, RXW_B, 0, 0);
        else if (x == 'B') rx_emit(r, RXI_WORD, RXW_NB, 0, 0);
        else if (x == '<') rx_emit(r, RXI_WORD, RXW_LT, 0, 0);
        else if (x == '>') rx_emit(r, RXI_WORD, RXW_GT, 0, 0);
        else rx_emit(r, RXI_CHAR, r->icase ? rx_lower(x) : (unsigned char)x, 0, 0);
    } else rx_emit(r, RXI_CHAR, r->icase ? rx_lower((unsigned char)c) : (unsigned char)c, 0, 0);
}

/* A count {m}, {m,}, {m,n} at p: where it ends, or -1 if it is not one. */
static int rx_count(const rx_t *r, int p, int hi, int *mn, int *mx) {
    int i = p + 1, m = 0, n, any = 0;
    while (i < hi && r->pat[i] >= '0' && r->pat[i] <= '9') { m = m * 10 + (r->pat[i] - '0'); i++; any = 1; }
    if (!any) return -1;
    n = m;
    if (i < hi && r->pat[i] == ',') {
        i++; n = -1; int k = 0, any2 = 0;
        while (i < hi && r->pat[i] >= '0' && r->pat[i] <= '9') { k = k * 10 + (r->pat[i] - '0'); i++; any2 = 1; }
        if (any2) n = k;
    }
    if (i >= hi || r->pat[i] != '}' || (n >= 0 && n < m) || m > 255 || n > 255) return -1;
    *mn = m; *mx = n;
    return i + 1;
}

static void rx_cat(rx_t *r, int lo, int hi) {
    int p = lo, lead = 1;
    while (p < hi && !r->err) {
        if (lead && (r->pat[p] == '*' || r->pat[p] == '+' || r->pat[p] == '?')) { p++; continue; }   /* repeats nothing */
        int e = rx_atom_end(r, p, hi);
        if (e < 0) return;
        int q = e, mn = 1, mx = 1;
        if (q < hi && (r->pat[q] == '*' || r->pat[q] == '+' || r->pat[q] == '?')) {
            char k = r->pat[q++];
            mn = k == '+' ? 1 : 0; mx = k == '?' ? 1 : -1;
        } else if (q < hi && r->pat[q] == '{') {
            int a, b, ce = rx_count(r, q, hi, &a, &b);
            if (ce > 0) { mn = a; mx = b; q = ce; }
        }
        while (q < hi && (r->pat[q] == '*' || r->pat[q] == '+' || r->pat[q] == '?')) {   /* a** a+? : as one */
            if (r->pat[q] != '+') mn = 0;
            if (r->pat[q] != '?') mx = -1;
            q++;
        }
        for (int k = 0; k < mn; k++) rx_gen_atom(r, p, e);
        if (mx < 0) {                                  /* the rest: any number */
            int l1 = rx_emit(r, RXI_SPLIT, 0, 0, 0);
            r->prog[l1].x = (short)(l1 + 1);
            rx_gen_atom(r, p, e);
            rx_emit(r, RXI_JMP, 0, l1, 0);
            r->prog[l1].y = (short)r->n;
        } else for (int k = mn; k < mx; k++) {         /* up to mx: each one optional */
            int l = rx_emit(r, RXI_SPLIT, 0, 0, 0);
            r->prog[l].x = (short)(l + 1);
            rx_gen_atom(r, p, e);
            r->prog[l].y = (short)r->n;
        }
        lead = 0;
        p = q;
    }
}

static void rx_alt(rx_t *r, int lo, int hi) {
    int bars[64], nb = 0, d = 0;
    for (int i = lo; i < hi; i++) {
        char c = r->pat[i];
        if (c == '\\') { i++; continue; }
        if (c == '[') { int e = rx_class_end(r, i); if (e < 0) break; i = e - 1; continue; }
        if (c == '(') d++;
        else if (c == ')') { if (d) d--; }
        else if (c == '|' && !d && nb < 64) bars[nb++] = i;
    }
    if (!nb) { rx_cat(r, lo, hi); return; }
    int jumps[64], nj = 0, start = lo;
    for (int k = 0; k <= nb; k++) {
        int end = k < nb ? bars[k] : hi;
        if (k < nb) {
            int s = rx_emit(r, RXI_SPLIT, 0, 0, 0);
            r->prog[s].x = (short)(s + 1);
            rx_cat(r, start, end);
            jumps[nj++] = rx_emit(r, RXI_JMP, 0, 0, 0);
            r->prog[s].y = (short)r->n;
        } else rx_cat(r, start, end);
        start = end + 1;
    }
    for (int k = 0; k < nj; k++) r->prog[jumps[k]].x = (short)r->n;
}

/* 0, or -1 with r->err saying why. */
static int rx_compile(rx_t *r, const char *pat, int icase) {
    r->n = 0; r->ncls = 0; r->ngrp = 0; r->icase = icase; r->err = 0;
    r->pat = pat;
    r->plen = 0;
    while (pat[r->plen]) r->plen++;
    if (r->plen >= 512) { r->err = "pattern too long"; return -1; }
    /* number the groups by their ( in the pattern, and make a ) with no (
     * a literal, as GNU's ERE does, by escaping it in a copy */
    char *copy = r->copy;
    int o = 0, d = 0;
    for (int i = 0; i < r->plen && o < 1020; i++) {
        char c = pat[i];
        if (c == '\\' && i + 1 < r->plen) { copy[o++] = c; copy[o++] = pat[++i]; continue; }
        if (c == '[') { int e = rx_class_end(r, i); if (e > 0) { while (i < e) copy[o++] = pat[i++]; i--; continue; } }
        if (c == '(') { d++; r->ngrp++; }
        if (c == ')') { if (!d) { copy[o++] = '\\'; copy[o++] = ')'; continue; } d--; }
        copy[o++] = c;
    }
    copy[o] = '\0';
    r->pat = copy; r->plen = o;
    if (o >= 512) { r->err = "pattern too long"; return -1; }
    int g = 0;
    for (int i = 0; i < o; i++) {
        if (copy[i] == '\\') { i++; continue; }
        if (copy[i] == '[') { int e = rx_class_end(r, i); if (e > 0) { i = e - 1; continue; } }
        if (copy[i] == '(') r->gidx[i] = (short)++g;
    }
    rx_emit(r, RXI_SAVE, 0, 0, 0);
    rx_alt(r, 0, o);
    rx_emit(r, RXI_SAVE, 0, 1, 0);
    rx_emit(r, RXI_MATCH, 0, 0, 0);
    if (r->ngrp > RX_MAXGRP) r->ngrp = RX_MAXGRP;
    return r->err ? -1 : 0;
}

/* ---- running it --------------------------------------------------------- */

#define RX_NCAP (2 * (RX_MAXGRP + 1))
typedef struct { int pc; int cap[RX_NCAP]; } rx_thread_t;
typedef struct { rx_thread_t *t; int n; } rx_list_t;

static int rx_word_ok(int kind, const char *s, int n, int i) {
    int before = i > 0 && rx_isword((unsigned char)s[i - 1]);
    int after = i < n && rx_isword((unsigned char)s[i]);
    if (kind == RXW_B) return before != after;
    if (kind == RXW_NB) return before == after;
    if (kind == RXW_LT) return !before && after;
    return before && !after;
}

/* Add the thread at pc, and everything it reaches without reading. */
typedef struct { int pc, slot, val; } rx_frame_t;
static void rx_add(const rx_t *r, rx_list_t *l, int *mark, int gen, rx_frame_t *stk,
                   int pc0, int *cap, const char *s, int n, int i) {
    int sp = 0;
    stk[sp].pc = pc0; stk[sp].slot = -1; sp++;
    while (sp) {
        rx_frame_t f = stk[--sp];
        if (f.slot >= 0) { cap[f.slot] = f.val; continue; }        /* a SAVE undone */
        int pc = f.pc;
        if (mark[pc] == gen) continue;
        mark[pc] = gen;
        const rx_inst_t *in = &r->prog[pc];
        switch (in->op) {
        case RXI_JMP: stk[sp].pc = in->x; stk[sp].slot = -1; sp++; break;
        case RXI_SPLIT:                                 /* x first: pushed last */
            stk[sp].pc = in->y; stk[sp].slot = -1; sp++;
            stk[sp].pc = in->x; stk[sp].slot = -1; sp++;
            break;
        case RXI_SAVE:
            if (in->x < RX_NCAP) {
                stk[sp].slot = in->x; stk[sp].val = cap[in->x]; sp++;   /* undone after */
                cap[in->x] = i;
            }
            stk[sp].pc = pc + 1; stk[sp].slot = -1; sp++;
            break;
        case RXI_BOL: if (i == 0) { stk[sp].pc = pc + 1; stk[sp].slot = -1; sp++; } break;
        case RXI_EOL: if (i == n) { stk[sp].pc = pc + 1; stk[sp].slot = -1; sp++; } break;
        case RXI_WORD: if (rx_word_ok(in->c, s, n, i)) { stk[sp].pc = pc + 1; stk[sp].slot = -1; sp++; } break;
        default: {
            rx_thread_t *t = &l->t[l->n++];
            t->pc = pc;
            for (int k = 0; k < RX_NCAP; k++) t->cap[k] = cap[k];
        }
        }
    }
}

/* The leftmost-longest match in s[0..n) that starts at or after from:
 * 1 and sub[0..1] (and each group's, -1 if it took no part), or 0. -1 if
 * there was no memory for it. */
static int rx_exec(const rx_t *r, const char *s, int n, int from, int *sub) {
    int ni = r->n;
    rx_thread_t *ta = RX_ALLOC(sizeof(rx_thread_t) * (unsigned)ni);
    rx_thread_t *tb = RX_ALLOC(sizeof(rx_thread_t) * (unsigned)ni);
    int *mark = RX_ALLOC(sizeof(int) * (unsigned)ni);
    rx_frame_t *stk = RX_ALLOC(sizeof(rx_frame_t) * (unsigned)(3 * ni + 8));
    if (!ta || !tb || !mark || !stk) {
        if (ta) RX_FREE(ta);
        if (tb) RX_FREE(tb);
        if (mark) RX_FREE(mark);
        if (stk) RX_FREE(stk);
        return -1;
    }
    for (int k = 0; k < ni; k++) mark[k] = -1;
    rx_list_t cl = { ta, 0 }, nl = { tb, 0 };
    int cap[RX_NCAP], best[RX_NCAP], have = 0, gen = 0;
    for (int i = from; ; i++) {
        if (!have) {                                    /* a new start here, last in line */
            for (int k = 0; k < RX_NCAP; k++) cap[k] = -1;
            rx_add(r, &cl, mark, gen, stk, 0, cap, s, n, i);
        }
        if (!cl.n) break;
        int c = i < n ? (unsigned char)s[i] : -1;
        if (r->icase && c >= 0) c = rx_lower(c);
        gen++;
        nl.n = 0;
        for (int k = 0; k < cl.n; k++) {
            rx_thread_t *t = &cl.t[k];
            const rx_inst_t *in = &r->prog[t->pc];
            if (have && t->cap[0] > best[0]) continue;  /* starts later than a match: out */
            int ok = 0;
            switch (in->op) {
            case RXI_MATCH:
                if (!have || t->cap[0] < best[0] || (t->cap[0] == best[0] && t->cap[1] > best[1])) {
                    for (int q = 0; q < RX_NCAP; q++) best[q] = t->cap[q];
                    have = 1;
                }
                break;
            case RXI_CHAR:  ok = c >= 0 && c == in->c; break;
            case RXI_ANY:   ok = c >= 0 && c != '\n'; break;
            case RXI_CLASS: ok = c >= 0 && (rx_has(r->cls[in->c], c) || (r->icase && rx_has(r->cls[in->c], rx_upper(c)))); break;
            }
            if (ok) rx_add(r, &nl, mark, gen, stk, t->pc + 1, t->cap, s, n, i + 1);
        }
        rx_list_t sw = cl; cl = nl; nl = sw;
        if (i >= n) {                                   /* the end: what is left can only match */
            for (int k = 0; k < cl.n; k++) {
                rx_thread_t *t = &cl.t[k];
                if (r->prog[t->pc].op == RXI_MATCH && (!have || t->cap[0] < best[0] || (t->cap[0] == best[0] && t->cap[1] > best[1]))) {
                    for (int q = 0; q < RX_NCAP; q++) best[q] = t->cap[q];
                    have = 1;
                }
            }
            break;
        }
    }
    RX_FREE(ta); RX_FREE(tb); RX_FREE(mark); RX_FREE(stk);
    if (have) for (int q = 0; q < RX_NCAP; q++) sub[q] = best[q];
    return have;
}

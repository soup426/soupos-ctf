/* reckon.c - expr under a kitchen name (v0.60.124): work it out.
 *
 *   cook reckon.elf 6 '*' 7                  42
 *   cook reckon.elf length soup              4
 *   cook reckon.elf soup123 : '[a-z]*\([0-9]*\)'   123
 *
 * As GNU's expr, lowest first: | (the left unless it is null or 0, else
 * the right, else 0), & (the left unless either is null or 0, else 0),
 * < <= = == != >= > (as numbers when both are, else as text: 1 or 0),
 * + -, * / %, STRING : REGEX, then length STRING, substr STRING POS LEN,
 * index STRING CHARS, match STRING REGEX, + TOKEN (TOKEN as text), and
 * ( ). Integers are 64-bit. Status 0, 1 when the result is null or 0, 2
 * for a bad expression, a non-integer argument or division by zero.
 *
 * REGEX is a basic one, as expr's, anchored at the start: \( \) \{ \}
 * \| \+ \? are the operators and ( ) { } | + ? plain, * at the start is a
 * plain *, ^ only first and $ only last. It is turned into an extended one
 * for rx.c. With a \( \) the result is what the first one matched (or
 * nothing); without, how many characters matched.
 */
#include "ulib.h"
#define RX_ALLOC(n) malloc(n)
#define RX_FREE(p)  ((void)(p))
#include "rx.c"

typedef long long ll;
typedef struct { int isint; ll i; const char *s; } val_t;

static char **A;
static int na, at;

static __attribute__((noreturn)) void die(const char *a, const char *b) {
    eprint("reckon.elf: "); eprint(a); if (b) eprint(b); eprint("\n");
    sys_exit(2);
}
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int scmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static char *sdup(const char *s, int n) { char *d = malloc((unsigned)n + 1); for (int i = 0; i < n; i++) d[i] = s[i]; d[n] = '\0'; return d; }

static val_t vint(ll i) { val_t v; v.isint = 1; v.i = i; v.s = 0; return v; }
static val_t vstr(const char *s) { val_t v; v.isint = 0; v.i = 0; v.s = s; return v; }

static int as_int(const char *s, ll *out) {
    const char *c = s; int neg = 0;
    if (*c == '-') { neg = 1; c++; }
    if (!*c) return 0;
    ll v = 0;
    for (; *c; c++) { if (*c < '0' || *c > '9') return 0; v = v * 10 + (*c - '0'); }
    *out = neg ? -v : v;
    return 1;
}
static ll num(val_t v) {
    if (v.isint) return v.i;
    ll x;
    if (!as_int(v.s, &x)) die("non-integer argument", 0);
    return x;
}
static int null_(val_t v) {
    if (v.isint) return v.i == 0;
    const char *c = v.s;
    if (!*c) return 1;
    if (*c == '-') c++;
    do { if (*c != '0') return 0; } while (*++c);
    return 1;
}
static const char *text(val_t v) {
    if (!v.isint) return v.s;
    char d[24]; int n = 0; ll x = v.i < 0 ? -v.i : v.i;
    do { d[n++] = (char)('0' + (int)(x % 10)); x /= 10; } while (x);
    char *o = malloc(26); int k = 0;
    if (v.i < 0) o[k++] = '-';
    while (n) o[k++] = d[--n];
    o[k] = '\0';
    return o;
}

static const char *peek(void) { return at < na ? A[at] : 0; }
static int is(const char *w) { return at < na && seq(A[at], w); }
static const char *take(const char *after) {
    if (at >= na) die("syntax error: missing argument after ", after ? after : "");
    return A[at++];
}

/* expr's basic regex as rx.c's extended one */
static char *bre_to_ere(const char *b) {
    int n = slen(b);
    char *e = malloc((unsigned)n * 2 + 4);
    int o = 0, start = 1;
    for (int i = 0; i < n; i++) {
        char c = b[i];
        if (c == '[') {                                 /* a bracket: as it is */
            int j = i + 1;
            if (j < n && b[j] == '^') j++;
            if (j < n && b[j] == ']') j++;
            while (j < n && b[j] != ']') j++;
            while (i <= j && i < n) e[o++] = b[i++];
            i--; start = 0; continue;
        }
        if (c == '\\' && i + 1 < n) {
            char x = b[++i];
            if (x == '(' || x == '|') { e[o++] = x; start = 1; continue; }
            if (x == ')' || x == '{' || x == '}' || x == '+' || x == '?') { e[o++] = x; start = 0; continue; }
            e[o++] = '\\'; e[o++] = x; start = 0; continue;
        }
        if (c == '(' || c == ')' || c == '{' || c == '}' || c == '|' || c == '+' || c == '?') { e[o++] = '\\'; e[o++] = c; start = 0; continue; }
        if (c == '*' && start) { e[o++] = '\\'; e[o++] = '*'; start = 0; continue; }
        if (c == '^') {
            if (start) { e[o++] = '^'; continue; }       /* still at the start: a * after it is plain */
            e[o++] = '\\'; e[o++] = '^'; continue;
        }
        if (c == '$') {
            int last = i + 1 == n || (b[i + 1] == '\\' && i + 2 < n && (b[i + 2] == ')' || b[i + 2] == '|'));
            if (!last) e[o++] = '\\';
            e[o++] = '$'; start = 0; continue;
        }
        e[o++] = c; start = 0;
    }
    e[o] = '\0';
    return e;
}

static val_t colon(val_t s, val_t r) {
    static rx_t rx;
    const char *str = text(s), *re = bre_to_ere(text(r));
    if (rx_compile(&rx, re, 0) < 0) die(rx.err, 0);
    int sub[RX_NCAP];
    int m = rx_exec(&rx, str, slen(str), 0, sub) > 0 && sub[0] == 0;
    if (rx.ngrp) {
        if (!m || sub[2] < 0) return vstr("");
        return vstr(sdup(str + sub[2], sub[3] - sub[2]));
    }
    return vint(m ? sub[1] : 0);
}

static val_t e_or(void);
static val_t e_atom(void) {
    const char *w = take(at ? A[at - 1] : 0);
    if (seq(w, "(")) {
        val_t v = e_or();
        if (!is(")")) die(at < na ? "syntax error: expecting ')' instead of " : "syntax error: expecting ')' after ", at < na ? A[at] : A[at - 1]);
        at++;
        return v;
    }
    if (seq(w, ")")) die("syntax error: unexpected ')'", 0);
    return vstr(w);
}
static val_t e_key(void) {
    if (is("+")) { at++; return vstr(take("+")); }
    if (is("length")) { at++; val_t s = e_key(); return vint(slen(text(s))); }
    if (is("match")) { at++; val_t s = e_key(), r = e_key(); return colon(s, r); }
    if (is("index")) {
        at++; val_t s = e_key(), c = e_key();
        const char *a = text(s), *cs = text(c);
        for (int i = 0; a[i]; i++) for (int k = 0; cs[k]; k++) if (a[i] == cs[k]) return vint(i + 1);
        return vint(0);
    }
    if (is("substr")) {
        at++; val_t s = e_key(), p = e_key(), l = e_key();
        const char *a = text(s); ll pos = num(p), len = num(l), n = slen(a);
        if (pos < 1 || len < 1 || pos > n) return vstr("");
        if (len > n - pos + 1) len = n - pos + 1;
        return vstr(sdup(a + pos - 1, (int)len));
    }
    return e_atom();
}
static val_t e_colon(void) {
    val_t l = e_key();
    while (is(":")) { at++; val_t r = e_key(); l = colon(l, r); }
    return l;
}
static val_t e_mul(void) {
    val_t l = e_colon();
    while (is("*") || is("/") || is("%")) {
        char op = A[at++][0];
        val_t r = e_colon();
        ll a = num(l), b = num(r);
        if (op != '*' && b == 0) die("division by zero", 0);
        l = vint(op == '*' ? a * b : op == '/' ? a / b : a % b);
    }
    return l;
}
static val_t e_add(void) {
    val_t l = e_mul();
    while (is("+") || is("-")) {
        char op = A[at++][0];
        val_t r = e_mul();
        ll a = num(l), b = num(r);
        l = vint(op == '+' ? a + b : a - b);
    }
    return l;
}
static val_t e_cmp(void) {
    val_t l = e_add();
    for (;;) {
        const char *op = peek();
        if (!op || !(seq(op, "<") || seq(op, "<=") || seq(op, "=") || seq(op, "==") || seq(op, "!=") || seq(op, ">=") || seq(op, ">"))) break;
        at++;
        val_t r = e_add();
        ll a, b; int c;
        const char *ta = text(l), *tb = text(r);
        if (as_int(ta, &a) && as_int(tb, &b)) c = a < b ? -1 : a > b;
        else c = scmp(ta, tb);
        int t = seq(op, "<") ? c < 0 : seq(op, "<=") ? c <= 0 : seq(op, "=") || seq(op, "==") ? c == 0 :
                seq(op, "!=") ? c != 0 : seq(op, ">=") ? c >= 0 : c > 0;
        l = vint(t);
    }
    return l;
}
static val_t e_and(void) {
    val_t l = e_cmp();
    while (is("&")) {
        at++;
        val_t r = e_cmp();
        if (null_(l) || null_(r)) l = vint(0);
    }
    return l;
}
static val_t e_or(void) {
    val_t l = e_and();
    while (is("|")) {
        at++;
        val_t r = e_and();
        if (null_(l)) { l = r; if (null_(l)) l = vint(0); }
    }
    return l;
}

int main(void) {
    static char *tok[64];
    na = uargv(tok, 64);
    A = tok;
    if (!na) die("missing operand", 0);
    val_t v = e_or();
    if (at < na) die("syntax error: unexpected argument ", A[at]);
    print(text(v)); print("\n");
    return null_(v) ? 1 : 0;
}

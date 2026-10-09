/* portion.c - a number cut into its primes, factor under a kitchen name
 * (v0.60.112).
 *
 *   cook portion.elf 360           360: 2 2 2 3 3 5
 *   slurp 12 35 | cook portion.elf
 *
 * As GNU's factor: N, a colon, its prime factors in order, a line per
 * number, from the words or else stdin, up to 2^64-1. Trial division by
 * the small primes, Miller-Rabin (bases exact below 2^64) and
 * Pollard-Brent rho for what is left. Not a number: said on stderr, the
 * rest still done, status 1.
 */
#include "ulib.h"

typedef unsigned long long u64;

static u64 addmod(u64 a, u64 b, u64 m) { return a >= m - b ? a - (m - b) : a + b; }
static u64 mulmod(u64 a, u64 b, u64 m) {
    if (m <= 0xFFFFFFFFull) return (a % m) * (b % m) % m;   /* fits: one division */
    u64 r = 0;
    a %= m; b %= m;
    while (b) {                                           /* double and add */
        if (b & 1) r = addmod(r, a, m);
        a = addmod(a, a, m);
        b >>= 1;
    }
    return r;
}
static u64 powmod(u64 a, u64 e, u64 m) {
    u64 r = 1 % m;
    a %= m;
    while (e) { if (e & 1) r = mulmod(r, a, m); a = mulmod(a, a, m); e >>= 1; }
    return r;
}
static int is_prime(u64 n) {
    if (n < 2) return 0;
    static const unsigned sp[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
    for (int i = 0; i < 12; i++) { if (n == sp[i]) return 1; if (n % sp[i] == 0) return 0; }
    u64 d = n - 1; int s = 0;
    while (!(d & 1)) { d >>= 1; s++; }
    for (int i = 0; i < 12; i++) {                        /* exact below 2^64 */
        u64 x = powmod(sp[i], d, n);
        if (x == 1 || x == n - 1) continue;
        int comp = 1;
        for (int r = 1; r < s; r++) { x = mulmod(x, x, n); if (x == n - 1) { comp = 0; break; } }
        if (comp) return 0;
    }
    return 1;
}
static u64 gcd(u64 a, u64 b) { while (b) { u64 t = a % b; a = b; b = t; } return a; }
static u64 absdiff(u64 a, u64 b) { return a > b ? a - b : b - a; }

/* A factor of composite n, which has no small factors: Brent's cycle
 * finding, the differences multiplied together 128 at a time so that one
 * gcd covers them all; on overshooting, back over the last batch singly. */
static u64 rho(u64 n) {
    for (u64 c = 1; ; c++) {
        u64 y = 2, x = 2, q = 1, g = 1, ys = 2;
        u64 m = 128, r = 1;
        do {
            x = y;
            for (u64 i = 0; i < r; i++) y = addmod(mulmod(y, y, n), c, n);
            u64 k = 0;
            do {
                ys = y;
                for (u64 i = 0; i < m && i < r - k; i++) {
                    y = addmod(mulmod(y, y, n), c, n);
                    q = mulmod(q, absdiff(x, y), n);
                }
                g = gcd(q, n);
                k += m;
            } while (k < r && g == 1);
            r <<= 1;
        } while (g == 1);
        if (g == n) {
            do { ys = addmod(mulmod(ys, ys, n), c, n); g = gcd(absdiff(x, ys), n); } while (g == 1);
        }
        if (g != n) return g;                             /* else another c */
    }
}

static u64 found[64];
static int nfound;
static void split(u64 n) {
    if (n == 1) return;
    if (is_prime(n)) { if (nfound < 64) found[nfound++] = n; return; }
    u64 d = rho(n);
    split(d); split(n / d);
}

static void put_u64(char *out, int *o, u64 v) {
    char d[24]; int n = 0;
    do { d[n++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
    while (n) out[(*o)++] = d[--n];
}

static int one(const char *w) {
    u64 n = 0;
    const char *c = w;
    if (*c == '+') c++;
    if (!*c) goto bad;
    for (; *c; c++) {
        if (*c < '0' || *c > '9') goto bad;
        unsigned dg = (unsigned)(*c - '0');
        if (n > 1844674407370955161ull || (n == 1844674407370955161ull && dg > 5)) goto bad;   /* over 2^64-1 */
        n = n * 10 + dg;
    }
    {
        static char out[1024]; int o = 0;
        put_u64(out, &o, n); out[o++] = ':';
        nfound = 0;
        u64 m = n;
        if (m > 1) {
            for (u64 p = 2; p < 10000 && p * p <= m; p += (p == 2 ? 1 : 2))
                while (m % p == 0) { found[nfound++] = p; m /= p; }
            split(m);
        }
        for (int i = 1; i < nfound; i++)                  /* in order */
            for (int j = i; j > 0 && found[j - 1] > found[j]; j--) { u64 t = found[j]; found[j] = found[j - 1]; found[j - 1] = t; }
        for (int i = 0; i < nfound; i++) { out[o++] = ' '; put_u64(out, &o, found[i]); }
        out[o++] = '\n';
        sys_write(FD_STDOUT, out, o);
        return 0;
    }
bad:
    eprint("portion.elf: '"); eprint(w); eprint("' is not a valid positive integer\n");
    return 1;
}

int main(void) {
    char *tok[32];
    int nt = uargv(tok, 32), st = 0;
    for (int a = 0; a < nt; a++) st |= one(tok[a]);
    if (nt) return st;
    static char buf[256], w[64];                          /* words from stdin */
    int wl = 0, got;
    while ((got = sys_read(FD_STDIN, buf, sizeof(buf))) > 0)
        for (int i = 0; i < got; i++) {
            char c = buf[i];
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') { if (wl) { w[wl] = 0; st |= one(w); wl = 0; } }
            else if (wl < 63) w[wl++] = c;
        }
    if (wl) { w[wl] = 0; st |= one(w); }
    return st;
}

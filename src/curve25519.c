/* curve25519.c - X25519 in sixteen 16-bit limbs. See curve25519.h. */
#include "curve25519.h"
#include "fe25519.h"
#include "str.h"

static const gf C121665 = { 0xDB41, 1 };

void fe_carry(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        /* The top limb's carry wraps to the bottom multiplied by 38, since
         * 2^256 = 38 (mod p). Every other limb's carry goes up one. */
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

/* Swap p and q if b is 1, in constant time. */
void fe_cswap(gf p, gf q, int b) {
    int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void fe_pack(uint8_t *o, const gf n) {
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    fe_carry(t); fe_carry(t); fe_carry(t);
    /* Reduce fully: subtract p, keep the result if it did not go negative. */
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        fe_cswap(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i]     = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

void fe_unpack(gf o, const uint8_t *n) {
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

void fe_add(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
void fe_sub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

void fe_mul(gf o, const gf a, const gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    fe_carry(o); fe_carry(o);
}
void fe_sqr(gf o, const gf a) { fe_mul(o, a, a); }

/* a^(p-2) = a^-1. The exponent p-2 is all ones except bits 2 and 4. */
void fe_inv(gf o, const gf i) {
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        fe_sqr(c, c);
        if (a != 2 && a != 4) fe_mul(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

void x25519(uint8_t q[32], const uint8_t n[32], const uint8_t p[32]) {
    uint8_t z[32];
    gf x, a, b, c, d, e, f;
    for (int i = 0; i < 31; i++) z[i] = n[i];
    z[31] = (uint8_t)((n[31] & 127) | 64);
    z[0] &= 248;
    fe_unpack(x, p);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    /* The Montgomery ladder: one differential add-and-double per scalar bit,
     * with a swap instead of a branch. */
    for (int i = 254; i >= 0; --i) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        fe_cswap(a, b, r); fe_cswap(c, d, r);
        fe_add(e, a, c); fe_sub(a, a, c);
        fe_add(c, b, d); fe_sub(b, b, d);
        fe_sqr(d, e);    fe_sqr(f, a);
        fe_mul(a, c, a); fe_mul(c, b, e);
        fe_add(e, a, c); fe_sub(a, a, c);
        fe_sqr(b, a);    fe_sub(c, d, f);
        fe_mul(a, c, C121665);
        fe_add(a, a, d); fe_mul(c, c, a);
        fe_mul(a, d, f); fe_mul(d, b, x);
        fe_sqr(b, e);
        fe_cswap(a, b, r); fe_cswap(c, d, r);
    }
    fe_inv(c, c);
    fe_mul(a, a, c);
    fe_pack(q, a);
}

void x25519_base(uint8_t q[32], const uint8_t n[32]) {
    static const uint8_t nine[32] = { 9 };
    x25519(q, n, nine);
}

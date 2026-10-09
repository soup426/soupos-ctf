/* ed25519.c - Ed25519 over the shared field. See ed25519.h.
 *
 * Points are extended coordinates (X, Y, Z, T) with T = XY/Z, so the unified
 * addition formula works for doubling too and the scalar ladder has no
 * special cases. Scalars mod the group order L are reduced with the
 * schoolbook routine at the bottom, which is the one piece of arithmetic
 * here that is not over the field.
 */
#include "ed25519.h"
#include "fe25519.h"
#include "sha512.h"
#include "str.h"

static const gf gf0 = { 0 };
static const gf gf1 = { 1 };
static const gf D  = { 0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
                       0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203 };
static const gf D2 = { 0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                       0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406 };
static const gf X  = { 0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                       0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169 };
static const gf Y  = { 0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                       0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666 };
static const gf I  = { 0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
                       0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83 };
static const int64_t L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10 };

static void set(gf r, const gf a) { for (int i = 0; i < 16; i++) r[i] = a[i]; }

static int neq(const gf a, const gf b) {
    uint8_t c[32], d[32];
    fe_pack(c, a); fe_pack(d, b);
    return memcmp(c, d, 32) != 0;
}

static uint8_t parity(const gf a) {
    uint8_t d[32];
    fe_pack(d, a);
    return d[0] & 1;
}

/* a^((p-5)/8), for the square root in point decompression. */
static void pow2523(gf o, const gf i) {
    gf c;
    set(c, i);
    for (int a = 250; a >= 0; a--) {
        fe_sqr(c, c);
        if (a != 1) fe_mul(c, c, i);
    }
    set(o, c);
}

/* p += q, extended coordinates, the unified formula. */
static void pt_add(gf p[4], const gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    fe_sub(a, p[1], p[0]); fe_sub(t, q[1], q[0]); fe_mul(a, a, t);
    fe_add(b, p[0], p[1]); fe_add(t, q[0], q[1]); fe_mul(b, b, t);
    fe_mul(c, p[3], q[3]); fe_mul(c, c, D2);
    fe_mul(d, p[2], q[2]); fe_add(d, d, d);
    fe_sub(e, b, a); fe_sub(f, d, c); fe_add(g, d, c); fe_add(h, b, a);
    fe_mul(p[0], e, f); fe_mul(p[1], h, g); fe_mul(p[2], g, f); fe_mul(p[3], e, h);
}

static void pt_cswap(gf p[4], gf q[4], int b) {
    for (int i = 0; i < 4; i++) fe_cswap(p[i], q[i], b);
}

static void pt_pack(uint8_t *r, gf p[4]) {
    gf tx, ty, zi;
    fe_inv(zi, p[2]);
    fe_mul(tx, p[0], zi);
    fe_mul(ty, p[1], zi);
    fe_pack(r, ty);
    r[31] ^= (uint8_t)(parity(tx) << 7);
}

/* p = s * q. */
static void pt_mul(gf p[4], gf q[4], const uint8_t *s) {
    set(p[0], gf0); set(p[1], gf1); set(p[2], gf1); set(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        int b = (s[i / 8] >> (i & 7)) & 1;
        pt_cswap(p, q, b);
        pt_add(q, p);
        pt_add(p, p);
        pt_cswap(p, q, b);
    }
}

static void pt_mul_base(gf p[4], const uint8_t *s) {
    gf q[4];
    set(q[0], X); set(q[1], Y); set(q[2], gf1);
    fe_mul(q[3], X, Y);
    pt_mul(p, q, s);
}

/* Reduce a 512-bit little-endian integer in x (one byte per word, possibly
 * carrying more than a byte) mod L into 32 bytes at r. */
static void mod_l(uint8_t *r, int64_t x[64]) {
    int64_t carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; j++) x[j] -= carry * L[j];
    for (i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

static void reduce(uint8_t *r) {            /* 64 bytes in, 32 meaningful out */
    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = (int64_t)r[i];
    for (int i = 0; i < 64; i++) r[i] = 0;
    mod_l(r, x);
}

/* Decompress a public key into the NEGATED point, which is what verification
 * wants. -1 if the bytes are not on the curve. */
static int unpack_neg(gf r[4], const uint8_t p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    set(r[2], gf1);
    fe_unpack(r[1], p);
    fe_sqr(num, r[1]);
    fe_mul(den, num, D);
    fe_sub(num, num, r[2]);
    fe_add(den, r[2], den);

    fe_sqr(den2, den); fe_sqr(den4, den2); fe_mul(den6, den4, den2);
    fe_mul(t, den6, num); fe_mul(t, t, den);
    pow2523(t, t);
    fe_mul(t, t, num); fe_mul(t, t, den); fe_mul(t, t, den);
    fe_mul(r[0], t, den);

    fe_sqr(chk, r[0]); fe_mul(chk, chk, den);
    if (neq(chk, num)) fe_mul(r[0], r[0], I);
    fe_sqr(chk, r[0]); fe_mul(chk, chk, den);
    if (neq(chk, num)) return -1;

    if (parity(r[0]) == (p[31] >> 7)) fe_sub(r[0], gf0, r[0]);
    fe_mul(r[3], r[0], r[1]);
    return 0;
}

static void expand_seed(uint8_t d[64], const uint8_t seed[32]) {
    sha512(seed, 32, d);
    d[0]  &= 248;
    d[31] &= 127;
    d[31] |= 64;
}

void ed25519_public_key(uint8_t pk[32], const uint8_t seed[32]) {
    uint8_t d[64];
    gf p[4];
    expand_seed(d, seed);
    pt_mul_base(p, d);
    pt_pack(pk, p);
}

void ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t len,
                  const uint8_t seed[32], const uint8_t pk[32]) {
    uint8_t d[64], r[64], h[64];
    gf p[4];
    sha512_t s;
    expand_seed(d, seed);

    /* r = H(prefix || msg) mod L; R = rB. */
    sha512_init(&s);
    sha512_update(&s, d + 32, 32);
    sha512_update(&s, msg, len);
    sha512_final(&s, r);
    reduce(r);
    pt_mul_base(p, r);
    pt_pack(sig, p);

    /* h = H(R || pk || msg) mod L; S = r + h*a mod L. */
    sha512_init(&s);
    sha512_update(&s, sig, 32);
    sha512_update(&s, pk, 32);
    sha512_update(&s, msg, len);
    sha512_final(&s, h);
    reduce(h);

    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = 0;
    for (int i = 0; i < 32; i++) x[i] = (int64_t)r[i];
    for (int i = 0; i < 32; i++)
        for (int j = 0; j < 32; j++) x[i + j] += (int64_t)h[i] * d[j];
    mod_l(sig + 32, x);
}

int ed25519_verify(const uint8_t sig[64], const uint8_t *msg, size_t len,
                   const uint8_t pk[32]) {
    gf p[4], q[4];
    uint8_t h[64], t[32];
    if (unpack_neg(q, pk) < 0) return -1;

    sha512_t s;
    sha512_init(&s);
    sha512_update(&s, sig, 32);
    sha512_update(&s, pk, 32);
    sha512_update(&s, msg, len);
    sha512_final(&s, h);
    reduce(h);

    /* SB - hA == R, with A already negated by unpack_neg. */
    pt_mul(p, q, h);
    pt_mul_base(q, sig + 32);
    pt_add(p, q);
    pt_pack(t, p);
    return memcmp(sig, t, 32) == 0 ? 0 : -1;
}

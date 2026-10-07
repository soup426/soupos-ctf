/* chacha.c - ChaCha20, Poly1305, and the RFC 8439 AEAD. See chacha.h. */
#include "chacha.h"
#include "str.h"

static inline uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

/* ── ChaCha20 ───────────────────────────────────────────────────────────── */

#define QR(a, b, c, d)                                   \
    a += b; d ^= a; d = rotl(d, 16);                     \
    c += d; b ^= c; b = rotl(b, 12);                     \
    a += b; d ^= a; d = rotl(d, 8);                      \
    c += d; b ^= c; b = rotl(b, 7);

static void chacha20_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    memcpy(x, in, sizeof(x));
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8],  x[12]); QR(x[1], x[5], x[9],  x[13]);
        QR(x[2], x[6], x[10], x[14]); QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]); QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8],  x[13]); QR(x[3], x[4], x[9],  x[14]);
    }
    for (int i = 0; i < 16; i++) put_le32(out + 4 * i, x[i] + in[i]);
}

static void chacha20_state(uint32_t s[16], const uint8_t key[32],
                           const uint8_t nonce[12], uint32_t counter) {
    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) s[4 + i] = le32(key + 4 * i);
    s[12] = counter;
    s[13] = le32(nonce); s[14] = le32(nonce + 4); s[15] = le32(nonce + 8);
}

void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                  uint32_t counter, uint8_t *buf, size_t len) {
    uint32_t s[16];
    uint8_t ks[64];
    chacha20_state(s, key, nonce, counter);
    while (len) {
        chacha20_block(s, ks);
        s[12]++;
        size_t n = len < 64 ? len : 64;
        for (size_t i = 0; i < n; i++) buf[i] ^= ks[i];
        buf += n; len -= n;
    }
}

/* ── Poly1305 ───────────────────────────────────────────────────────────── */

void poly1305_init(poly1305_t *p, const uint8_t key[32]) {
    /* r, clamped, spread over five 26-bit limbs. */
    p->r[0] = (le32(key +  0)     ) & 0x3ffffff;
    p->r[1] = (le32(key +  3) >> 2) & 0x3ffff03;
    p->r[2] = (le32(key +  6) >> 4) & 0x3ffc0ff;
    p->r[3] = (le32(key +  9) >> 6) & 0x3f03fff;
    p->r[4] = (le32(key + 12) >> 8) & 0x00fffff;
    for (int i = 0; i < 5; i++) p->h[i] = 0;
    for (int i = 0; i < 4; i++) p->pad[i] = le32(key + 16 + 4 * i);
    p->buf_len = 0;
}

static void poly1305_blocks(poly1305_t *p, const uint8_t *m, size_t len, uint32_t hibit) {
    uint32_t r0 = p->r[0], r1 = p->r[1], r2 = p->r[2], r3 = p->r[3], r4 = p->r[4];
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4];

    while (len >= 16) {
        h0 += (le32(m +  0)     ) & 0x3ffffff;
        h1 += (le32(m +  3) >> 2) & 0x3ffffff;
        h2 += (le32(m +  6) >> 4) & 0x3ffffff;
        h3 += (le32(m +  9) >> 6) & 0x3ffffff;
        h4 += (le32(m + 12) >> 8) | hibit;

        uint64_t d0 = (uint64_t)h0*r0 + (uint64_t)h1*s4 + (uint64_t)h2*s3 + (uint64_t)h3*s2 + (uint64_t)h4*s1;
        uint64_t d1 = (uint64_t)h0*r1 + (uint64_t)h1*r0 + (uint64_t)h2*s4 + (uint64_t)h3*s3 + (uint64_t)h4*s2;
        uint64_t d2 = (uint64_t)h0*r2 + (uint64_t)h1*r1 + (uint64_t)h2*r0 + (uint64_t)h3*s4 + (uint64_t)h4*s3;
        uint64_t d3 = (uint64_t)h0*r3 + (uint64_t)h1*r2 + (uint64_t)h2*r1 + (uint64_t)h3*r0 + (uint64_t)h4*s4;
        uint64_t d4 = (uint64_t)h0*r4 + (uint64_t)h1*r3 + (uint64_t)h2*r2 + (uint64_t)h3*r1 + (uint64_t)h4*r0;

        uint32_t c;
        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff; d1 += c;
        c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff; d2 += c;
        c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff; d3 += c;
        c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff; d4 += c;
        c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff;
        h0 += c * 5;
        c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

        m += 16; len -= 16;
    }
    p->h[0] = h0; p->h[1] = h1; p->h[2] = h2; p->h[3] = h3; p->h[4] = h4;
}

void poly1305_update(poly1305_t *p, const uint8_t *m, size_t len) {
    if (p->buf_len) {
        size_t take = 16 - p->buf_len;
        if (take > len) take = len;
        memcpy(p->buf + p->buf_len, m, take);
        p->buf_len += take; m += take; len -= take;
        if (p->buf_len < 16) return;
        poly1305_blocks(p, p->buf, 16, 1u << 24);
        p->buf_len = 0;
    }
    size_t whole = len & ~(size_t)15;
    if (whole) { poly1305_blocks(p, m, whole, 1u << 24); m += whole; len -= whole; }
    if (len) { memcpy(p->buf, m, len); p->buf_len = len; }
}

void poly1305_final(poly1305_t *p, uint8_t tag[16]) {
    if (p->buf_len) {
        /* The last block is padded with a single 1 bit then zeros, and the
         * usual high bit is not added. */
        p->buf[p->buf_len++] = 1;
        while (p->buf_len < 16) p->buf[p->buf_len++] = 0;
        poly1305_blocks(p, p->buf, 16, 0);
    }
    uint32_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4], c;
    c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    /* Compute h + -p and select it if h >= p. */
    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1u << 26);
    uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

    /* Back to four 32-bit words, add the pad with carry, serialise. */
    h0 = (h0      ) | (h1 << 26);
    h1 = (h1 >>  6) | (h2 << 20);
    h2 = (h2 >> 12) | (h3 << 14);
    h3 = (h3 >> 18) | (h4 <<  8);
    uint64_t f;
    f = (uint64_t)h0 + p->pad[0];             h0 = (uint32_t)f;
    f = (uint64_t)h1 + p->pad[1] + (f >> 32); h1 = (uint32_t)f;
    f = (uint64_t)h2 + p->pad[2] + (f >> 32); h2 = (uint32_t)f;
    f = (uint64_t)h3 + p->pad[3] + (f >> 32); h3 = (uint32_t)f;
    put_le32(tag, h0); put_le32(tag + 4, h1); put_le32(tag + 8, h2); put_le32(tag + 12, h3);
}

/* ── AEAD ───────────────────────────────────────────────────────────────── */

static void aead_tag(const uint8_t key[32], const uint8_t nonce[12],
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *ct, size_t len, uint8_t tag[16]) {
    /* The one-time Poly1305 key is the first 32 bytes of block 0; the data
     * blocks start at counter 1. */
    uint8_t otk[32];
    memset(otk, 0, sizeof(otk));
    chacha20_xor(key, nonce, 0, otk, sizeof(otk));

    static const uint8_t zero[16] = {0};
    uint8_t lens[16];
    memset(lens, 0, sizeof(lens));
    put_le32(lens, (uint32_t)aad_len);
    put_le32(lens + 8, (uint32_t)len);

    poly1305_t p;
    poly1305_init(&p, otk);
    poly1305_update(&p, aad, aad_len);
    if (aad_len & 15) poly1305_update(&p, zero, 16 - (aad_len & 15));
    poly1305_update(&p, ct, len);
    if (len & 15) poly1305_update(&p, zero, 16 - (len & 15));
    poly1305_update(&p, lens, 16);
    poly1305_final(&p, tag);
}

void chacha20poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              uint8_t *buf, size_t len, uint8_t tag[16]) {
    chacha20_xor(key, nonce, 1, buf, len);
    aead_tag(key, nonce, aad, aad_len, buf, len, tag);
}

int chacha20poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                             const uint8_t *aad, size_t aad_len,
                             uint8_t *buf, size_t len, const uint8_t tag[16]) {
    uint8_t want[16];
    aead_tag(key, nonce, aad, aad_len, buf, len, want);
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= want[i] ^ tag[i];
    if (diff) return -1;
    chacha20_xor(key, nonce, 1, buf, len);
    return 0;
}

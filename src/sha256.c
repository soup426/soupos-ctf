/* sha256.c - SHA-256 and HMAC, straight from FIPS 180-4.
 *
 * Nothing clever: a 64-byte block buffer, the eight working words, and the
 * compression function written out as the standard describes it. The one
 * place a hand-written SHA-256 usually goes wrong is the padding tail, where
 * the message length must be big-endian 64-bit bits-not-bytes and the final
 * block may or may not spill into a second one; the 448-bit vector in `sample`
 * exists precisely because it is 56 bytes, the exact length that forces the
 * spill.
 */
#include "sha256.h"
#include "str.h"

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void compress(uint32_t h[8], const uint8_t block[SHA256_BLOCK]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = be32(block + 4 * i);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19)  ^ (w[i-2]  >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

void sha256_init(sha256_t *s) {
    s->h[0]=0x6a09e667; s->h[1]=0xbb67ae85; s->h[2]=0x3c6ef372; s->h[3]=0xa54ff53a;
    s->h[4]=0x510e527f; s->h[5]=0x9b05688c; s->h[6]=0x1f83d9ab; s->h[7]=0x5be0cd19;
    s->buf_len = 0;
    s->total   = 0;
}

void sha256_update(sha256_t *s, const void *data, size_t len) {
    const uint8_t *p = data;
    s->total += len;
    while (len) {
        size_t take = SHA256_BLOCK - s->buf_len;
        if (take > len) take = len;
        memcpy(s->buf + s->buf_len, p, take);
        s->buf_len += take; p += take; len -= take;
        if (s->buf_len == SHA256_BLOCK) { compress(s->h, s->buf); s->buf_len = 0; }
    }
}

void sha256_final(sha256_t *s, uint8_t out[SHA256_DIGEST]) {
    /* 0x80, then zeros, then the length in BITS, big-endian, in the last eight
     * bytes of a block. If fewer than eight bytes are left after the 0x80, the
     * length goes in the following block. */
    uint64_t bits = s->total * 8;
    uint8_t pad = 0x80;
    sha256_update(s, &pad, 1);
    uint8_t zero = 0;
    while (s->buf_len != 56) sha256_update(s, &zero, 1);
    uint8_t tail[8];
    for (int i = 0; i < 8; i++) tail[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, tail, 8);
    for (int i = 0; i < 8; i++) {
        out[4*i]   = (uint8_t)(s->h[i] >> 24);
        out[4*i+1] = (uint8_t)(s->h[i] >> 16);
        out[4*i+2] = (uint8_t)(s->h[i] >> 8);
        out[4*i+3] = (uint8_t)(s->h[i]);
    }
}

void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST]) {
    sha256_t s;
    sha256_init(&s);
    sha256_update(&s, data, len);
    sha256_final(&s, out);
}

void hmac_sha256(const void *key, size_t key_len,
                 const void *data, size_t data_len,
                 uint8_t out[SHA256_DIGEST]) {
    uint8_t k[SHA256_BLOCK];
    memset(k, 0, sizeof(k));
    if (key_len > SHA256_BLOCK) sha256(key, key_len, k);     /* RFC 2104 */
    else                        memcpy(k, key, key_len);

    uint8_t ipad[SHA256_BLOCK], opad[SHA256_BLOCK];
    for (int i = 0; i < SHA256_BLOCK; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }

    uint8_t inner[SHA256_DIGEST];
    sha256_t s;
    sha256_init(&s);
    sha256_update(&s, ipad, SHA256_BLOCK);
    sha256_update(&s, data, data_len);
    sha256_final(&s, inner);

    sha256_init(&s);
    sha256_update(&s, opad, SHA256_BLOCK);
    sha256_update(&s, inner, SHA256_DIGEST);
    sha256_final(&s, out);
}

/* ── PBKDF2-HMAC-SHA256 ─────────────────────────────────────────────────── */
static void hmac_states(const uint8_t *key, size_t key_len, sha256_t *in, sha256_t *out) {
    uint8_t k[SHA256_BLOCK];
    memset(k, 0, sizeof(k));
    if (key_len > SHA256_BLOCK) sha256(key, key_len, k);
    else                        memcpy(k, key, key_len);
    uint8_t ipad[SHA256_BLOCK], opad[SHA256_BLOCK];
    for (int i = 0; i < SHA256_BLOCK; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    sha256_init(in);  sha256_update(in,  ipad, SHA256_BLOCK);
    sha256_init(out); sha256_update(out, opad, SHA256_BLOCK);
}

static void hmac_from(const sha256_t *in, const sha256_t *out, const uint8_t *msg, size_t len,
                      uint8_t mac[SHA256_DIGEST]) {
    sha256_t s = *in;
    uint8_t inner[SHA256_DIGEST];
    sha256_update(&s, msg, len);
    sha256_final(&s, inner);
    s = *out;
    sha256_update(&s, inner, SHA256_DIGEST);
    sha256_final(&s, mac);
}

void pbkdf2_hmac_sha256(const void *pw, size_t pw_len, const void *salt, size_t salt_len,
                        uint32_t iterations, uint8_t *out, size_t out_len) {
    sha256_t in, outer;
    hmac_states(pw, pw_len, &in, &outer);
    uint8_t first[64 + 4];                 /* salt || INT(block), salts up to 64 bytes */
    if (salt_len > 64) salt_len = 64;
    for (uint32_t block = 1; out_len; block++) {
        memcpy(first, salt, salt_len);
        first[salt_len]     = (uint8_t)(block >> 24);
        first[salt_len + 1] = (uint8_t)(block >> 16);
        first[salt_len + 2] = (uint8_t)(block >> 8);
        first[salt_len + 3] = (uint8_t)block;
        uint8_t u[SHA256_DIGEST], t[SHA256_DIGEST];
        hmac_from(&in, &outer, first, salt_len + 4, u);
        memcpy(t, u, sizeof(t));
        for (uint32_t i = 1; i < iterations; i++) {
            hmac_from(&in, &outer, u, sizeof(u), u);
            for (int k = 0; k < SHA256_DIGEST; k++) t[k] ^= u[k];
        }
        size_t take = out_len < SHA256_DIGEST ? out_len : SHA256_DIGEST;
        memcpy(out, t, take);
        out += take; out_len -= take;
    }
}

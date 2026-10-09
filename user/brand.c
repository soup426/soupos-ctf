/* brand.c - SHA-256, sha256sum under a kitchen name (v0.60.107): the mark
 * that says which batch this was.
 *
 *   cook brand.elf FILE...      HASH  FILE, a line each
 *   cook spoon.elf F | cook brand.elf      HASH  -
 *
 * As GNU's sha256sum prints it: the hash in hex, two spaces, the name (-
 * for stdin). A file it cannot open is said and skipped, status 1.
 */
#include "ulib.h"

typedef unsigned int u32;
static const u32 K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

typedef struct { u32 h[8]; unsigned char buf[64]; u32 n; u32 lo, hi; } sha_t;

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void block(sha_t *s, const unsigned char *p) {
    u32 w[64];
    for (int i = 0; i < 16; i++) w[i] = (u32)p[4*i] << 24 | (u32)p[4*i+1] << 16 | (u32)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        u32 s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u32 a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        u32 t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        u32 t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void init(sha_t *s) {
    static const u32 iv[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    for (int i = 0; i < 8; i++) s->h[i] = iv[i];
    s->n = 0; s->lo = s->hi = 0;
}
static void feed(sha_t *s, const unsigned char *p, u32 len) {
    for (u32 i = 0; i < len; i++) {
        s->buf[s->n++] = p[i];
        if (s->n == 64) { block(s, s->buf); s->n = 0; }
    }
    u32 old = s->lo; s->lo += len * 8; if (s->lo < old) s->hi++;
    s->hi += len >> 29;
}
static void finish(sha_t *s, char *hex) {
    u32 lo = s->lo, hi = s->hi;
    s->buf[s->n++] = 0x80;
    if (s->n > 56) { while (s->n < 64) s->buf[s->n++] = 0; block(s, s->buf); s->n = 0; }
    while (s->n < 56) s->buf[s->n++] = 0;
    for (int i = 0; i < 4; i++) s->buf[56 + i] = (unsigned char)(hi >> (24 - 8 * i));
    for (int i = 0; i < 4; i++) s->buf[60 + i] = (unsigned char)(lo >> (24 - 8 * i));
    block(s, s->buf);
    static const char d[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) for (int j = 0; j < 4; j++) {
        unsigned char b = (unsigned char)(s->h[i] >> (24 - 8 * j));
        hex[8*i + 2*j] = d[b >> 4]; hex[8*i + 2*j + 1] = d[b & 15];
    }
    hex[64] = '\0';
}
static void sum(int fd, const char *name) {
    static sha_t s; static unsigned char buf[512]; char hex[65];
    init(&s);
    int got;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0) feed(&s, buf, (u32)got);
    finish(&s, hex);
    print(hex); print("  "); print(name); print("\n");
}

int main(void) {
    char *tok[16];
    int nt = uargv(tok, 16), st = 0;
    if (nt == 0) { sum(FD_STDIN, "-"); return 0; }
    for (int a = 0; a < nt; a++) {
        if (tok[a][0] == '-' && !tok[a][1]) { sum(FD_STDIN, "-"); continue; }
        int fd = sys_open(tok[a], O_READ);
        if (fd < 0) { eprint("brand.elf: "); eprint(tok[a]); eprint(": cannot open\n"); st = 1; continue; }   /* stderr, as GNU */
        sum(fd, tok[a]);
        sys_close(fd);
    }
    return st;
}

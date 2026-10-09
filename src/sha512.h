#pragma once
#include <stdint.h>
#include <stddef.h>

/* SHA-512, FIPS 180-4. Ed25519 hashes with this, not SHA-256, which is why
 * it exists. Same shape as sha256.c with 64-bit words, 80 rounds and a
 * 128-byte block; the round constants and IV were derived from the cube and
 * square roots of the first primes rather than typed in. Checked in `sample`
 * against the "abc" and 896-bit vectors. Not constant-time anywhere. */

#define SHA512_DIGEST 64
#define SHA512_BLOCK  128

typedef struct {
    uint64_t h[8];
    uint8_t  buf[SHA512_BLOCK];
    uint32_t buf_len;
    uint64_t total;
} sha512_t;

void sha512_init  (sha512_t *s);
void sha512_update(sha512_t *s, const void *data, size_t len);
void sha512_final (sha512_t *s, uint8_t out[SHA512_DIGEST]);
void sha512(const void *data, size_t len, uint8_t out[SHA512_DIGEST]);

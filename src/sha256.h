#pragma once
#include <stdint.h>
#include <stddef.h>

/* SHA-256 and HMAC-SHA256, the first of the SSH primitives.
 *
 * Written from the FIPS 180-4 description and checked against its published
 * vectors in `sample` - "abc", the 448-bit message, a million 'a's - plus the
 * RFC 4231 HMAC cases, including the one whose key is longer than a block.
 * The vectors ARE the evidence: a hash that produces plausible-looking output
 * for the wrong reason is indistinguishable from a right one until something
 * downstream refuses to agree with it.
 *
 * This is hand-written crypto in a hobby kernel. It is for learning and for a
 * private network, and it is constant-time nowhere. Say so wherever it is
 * advertised. */

#define SHA256_DIGEST 32
#define SHA256_BLOCK  64

typedef struct {
    uint32_t h[8];
    uint8_t  buf[SHA256_BLOCK];
    uint32_t buf_len;
    uint64_t total;             /* bytes fed in so far, for the length tail */
} sha256_t;

void sha256_init  (sha256_t *s);
void sha256_update(sha256_t *s, const void *data, size_t len);
void sha256_final (sha256_t *s, uint8_t out[SHA256_DIGEST]);

/* One call for the common case. */
void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST]);

/* HMAC-SHA256 (RFC 2104). A key longer than a block is hashed first, as the
 * RFC requires; the long-key vector in `sample` is what proves that path. */
/* PBKDF2 with HMAC-SHA256 (RFC 8018), for stored passwords (v0.46.0). The
 * HMAC inner and outer states are computed once, so an iteration costs two
 * compressions. Checked in `sample` against Python's hashlib.pbkdf2_hmac. */
void pbkdf2_hmac_sha256(const void *pw, size_t pw_len, const void *salt, size_t salt_len,
                        uint32_t iterations, uint8_t *out, size_t out_len);

void hmac_sha256(const void *key, size_t key_len,
                 const void *data, size_t data_len,
                 uint8_t out[SHA256_DIGEST]);

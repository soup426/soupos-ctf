#pragma once
#include <stdint.h>
#include <stddef.h>

/* ChaCha20, Poly1305 and their AEAD construction, from RFC 8439. Stage S3.
 *
 * SSH's chacha20-poly1305@openssh.com uses the pieces in its own particular
 * way (two keys, the packet length encrypted separately, the sequence number
 * as the nonce), so the pieces are exposed individually as well as the RFC's
 * combined AEAD. Each is checked in `sample` against the RFC's vectors: the
 * section 2.4.2 encryption, the 2.5.2 Poly1305 tag and the 2.8.2 AEAD, with
 * expected values regenerated from an independent library rather than copied
 * from this code's own output.
 *
 * Poly1305 is the 26-bit-limb arithmetic (five limbs, 64-bit products), which
 * is what fits a 32-bit machine without a bignum. Not constant-time: the tag
 * compare is, nothing else is. Learning code for a private network. */

/* XOR `len` bytes in place with the keystream for (key, nonce, counter). */
void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                  uint32_t counter, uint8_t *buf, size_t len);

typedef struct {
    uint32_t r[5], h[5], pad[4];
    uint8_t  buf[16];
    uint32_t buf_len;
} poly1305_t;

void poly1305_init  (poly1305_t *p, const uint8_t key[32]);
void poly1305_update(poly1305_t *p, const uint8_t *m, size_t len);
void poly1305_final (poly1305_t *p, uint8_t tag[16]);

/* RFC 8439 section 2.8. Encrypt `len` bytes in place and write the tag. */
void chacha20poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              uint8_t *buf, size_t len, uint8_t tag[16]);
/* Verify the tag, then decrypt in place. Returns 0, or -1 and leaves the
 * buffer untouched if the tag does not match. */
int  chacha20poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              uint8_t *buf, size_t len, const uint8_t tag[16]);

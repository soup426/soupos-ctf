#pragma once
#include <stdint.h>
#include <stddef.h>

/* Ed25519 signatures, RFC 8032. Stage S5: the host key SSH will prove itself
 * with. Shares the field with curve25519.c (fe25519.h); the curve constants
 * d, 2d, the base point, sqrt(-1) and the group order were derived in Python
 * from their definitions rather than copied. SHA-512 underneath, as the RFC
 * requires. Checked in `sample` against the RFC's first three vectors, plus
 * a signature that must fail to verify once a byte of it is changed.
 *
 * Signing does a fixed-base scalar multiplication with the simple ladder,
 * and verifying does two, so each takes a few ticks. Not constant-time
 * anywhere that matters; learning code for a private network. */

/* Public key from a 32-byte seed (the RFC's "private key"). */
void ed25519_public_key(uint8_t pk[32], const uint8_t seed[32]);

/* 64-byte signature over msg with the seed and its public key. */
void ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t len,
                  const uint8_t seed[32], const uint8_t pk[32]);

/* 0 if sig is a valid signature of msg under pk, -1 otherwise. */
int  ed25519_verify(const uint8_t sig[64], const uint8_t *msg, size_t len,
                    const uint8_t pk[32]);

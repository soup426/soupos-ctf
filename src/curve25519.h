#pragma once
#include <stdint.h>

/* X25519 key agreement, RFC 7748. Stage S4 of the SSH plan.
 *
 * The field arithmetic is the sixteen-limb, 16-bits-per-limb form in signed
 * 64-bit words, which is the simplest correct way to do a 255-bit field on a
 * 32-bit machine: every product fits an int64, the carry is one pass, and the
 * reduction uses nothing cleverer than the fact that 2^256 = 38 mod p. It is
 * slow next to a proper radix-2^25.5 implementation, and a key exchange
 * happens once per connection, so that trade is the right one here.
 *
 * Checked in `sample` against the RFC's two scalar-multiplication vectors and
 * the iterated test at 1 and 1000 rounds. The iterated one is what catches a
 * bug in the arithmetic that the single vectors get lucky on: a limb that
 * carries wrong only for certain inputs will show up somewhere in a thousand
 * chained products.
 *
 * The ladder itself is constant-time in structure (conditional swaps, no
 * branches on the scalar); the C compiler and the 64-bit helper routines
 * make no such promise. Learning code for a private network. */

/* q = n * p on the curve. Clamps n as the RFC says. */
void x25519(uint8_t q[32], const uint8_t n[32], const uint8_t p[32]);

/* q = n * 9, the base point: a public key from a private one. */
void x25519_base(uint8_t q[32], const uint8_t n[32]);

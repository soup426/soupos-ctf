#pragma once
#include <stdint.h>

/* The GF(2^255-19) arithmetic from curve25519.c, shared with ed25519.c so
 * the field is implemented once. Sixteen 16-bit limbs in int64 words; see
 * curve25519.h for why that representation. Internal to the two files. */

typedef int64_t gf[16];

void fe_carry (gf o);
void fe_cswap (gf p, gf q, int b);
void fe_pack  (uint8_t *o, const gf n);
void fe_unpack(gf o, const uint8_t *n);
void fe_add   (gf o, const gf a, const gf b);
void fe_sub   (gf o, const gf a, const gf b);
void fe_mul   (gf o, const gf a, const gf b);
void fe_sqr   (gf o, const gf a);
void fe_inv   (gf o, const gf i);

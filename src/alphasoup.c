/* alphasoup.c - AlphaSOUP-32 non-cryptographic hash for soupOS
 *
 * Think of each byte as a letter of pasta getting stirred into a pot -
 * every letter changes the flavor (hash state).
 *
 * Design:
 *   SOUPSEED  - initial pot flavour (arbitrary odd constant)
 *   NOODLE    - golden-ratio multiplier for avalanche
 *   stir()    - rotate left (mix without losing bits)
 *   Each byte: XOR into state, rotate, multiply, XOR-shift (season)
 *   Finalise:  3-stage MurmurHash3-style avalanche
 */

#include "alphasoup.h"
#include <stdint.h>

#define SOUPSEED  0xB07B0C2Du
#define NOODLE    0x9E3779B9u   /* 2^32 / golden ratio */

/* Rotate left by n bits */
static uint32_t stir(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

uint32_t alphasoup_hash(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = SOUPSEED ^ (len * NOODLE);

    for (uint32_t i = 0; i < len; i++) {
        /* Each byte is a noodle dropped into the soup */
        h ^= (uint32_t)p[i];
        h  = stir(h, 13);   /* simmer: rotate left 13 */
        h *= NOODLE;         /* boil:   golden-ratio multiply */
        h ^= h >> 17;        /* season: XOR-shift */
    }

    /* Final avalanche - ensure every input bit affects every output bit */
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;

    return h;
}

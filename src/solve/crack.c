/* crack.c - stage 2 solver: find ANY preimage of a 32-bit AlphaSOUP hash.
 *
 * soupOS stores unsalted AlphaSOUP-32 hashes in /etc/kitchen and `chef`
 * accepts anything that hashes to the stored value, so we do not need the
 * real password - any collision is accepted.
 *
 *   cc -O3 -fopenmp -o crack solve/crack.c && ./crack 0xf63a9eb7
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define SOUPSEED 0xB07B0C2Du
#define NOODLE   0x9E3779B9u

static inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static inline uint32_t alphasoup(const uint8_t *p, uint32_t len) {
    uint32_t h = SOUPSEED ^ (len * NOODLE);
    for (uint32_t i = 0; i < len; i++) {
        h ^= p[i];
        h  = rotl(h, 13);
        h *= NOODLE;
        h ^= h >> 17;
    }
    h ^= h >> 16; h *= 0x85EBCA6Bu;
    h ^= h >> 13; h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <hash, e.g. 0xf63a9eb7>\n", argv[0]); return 1; }
    uint32_t target = (uint32_t)strtoul(argv[1], NULL, 0);
    const char *alpha = "abcdefghijklmnopqrstuvwxyz0123456789";
    const int A = 36;
    volatile int done = 0;
    printf("searching for a preimage of 0x%08x ...\n", target);

    for (int len = 1; len <= 7 && !done; len++) {
        unsigned long long space = 1;
        for (int i = 0; i < len; i++) space *= A;
        #pragma omp parallel for schedule(static)
        for (long long n = 0; n < (long long)space; n++) {
            if (done) continue;
            uint8_t buf[8];
            long long v = n;
            for (int i = 0; i < len; i++) { buf[i] = (uint8_t)alpha[v % A]; v /= A; }
            if (alphasoup(buf, (uint32_t)len) == target) {
                buf[len] = 0;
                printf("FOUND: \"%s\"  (len %d, hashes to 0x%08x)\n",
                       buf, len, alphasoup(buf, (uint32_t)len));
                done = 1;
            }
        }
        if (!done) printf("  len %d exhausted (%llu candidates)\n", len, space);
    }
    return done ? 0 : 2;
}

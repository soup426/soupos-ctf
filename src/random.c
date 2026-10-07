/* random.c - the entropy pool. See random.h for what feeds it.
 *
 * The interrupt-side cost matters, because random_stir runs a hundred times a
 * second from the timer. A stir is one XOR into a 16-word input block; only
 * when the block has wrapped does it fold into the pool with a hash, so the
 * compression function runs about six times a second, not a hundred.
 */
#include "random.h"
#include "sha256.h"
#include "rtc.h"
#include "str.h"
#include "klog.h"

static uint8_t  pool[SHA256_DIGEST];
static uint32_t in[16];
static uint32_t in_idx;
static uint32_t counter;
static int      has_rdrand;
static uint32_t stirs;

static inline uint32_t tsc_lo(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return lo;
}

static int rdrand32(uint32_t *out) {
    uint8_t ok;
    __asm__ volatile ("rdrand %0; setc %1" : "=r"(*out), "=qm"(ok));
    return ok;
}

static void fold(void) {
    sha256_t s;
    sha256_init(&s);
    sha256_update(&s, pool, sizeof(pool));
    sha256_update(&s, in, sizeof(in));
    sha256_final(&s, pool);
    memset(in, 0, sizeof(in));
}

void random_stir(uint32_t v) {
    in[in_idx & 15] ^= tsc_lo() ^ v;
    stirs++;
    if ((++in_idx & 15) == 0) fold();
}

int random_has_rdrand(void) { return has_rdrand; }

void random_init(void) {
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    has_rdrand = (c >> 30) & 1;

    rtc_time_t t;
    rtc_read(&t);
    random_stir((uint32_t)t.second | (uint32_t)t.minute << 8 |
                (uint32_t)t.hour << 16 | (uint32_t)t.day << 24);
    random_stir((uint32_t)t.month | (uint32_t)t.year << 8);
    for (int i = 0; i < 16; i++) {
        uint32_t r = 0;
        if (has_rdrand) rdrand32(&r);
        random_stir(r);
    }
    fold();
    klog("[random] pool seeded (rdrand %s)\n", has_rdrand ? "present" : "absent");
}

void random_bytes(void *buf, size_t n) {
    uint8_t *p = buf;
    while (n) {
        uint32_t r = 0;
        if (has_rdrand) rdrand32(&r);
        uint32_t extra[3] = { counter++, tsc_lo(), r };

        uint8_t out[SHA256_DIGEST];
        sha256_t s;
        sha256_init(&s);
        sha256_update(&s, pool, sizeof(pool));
        sha256_update(&s, extra, sizeof(extra));
        sha256_final(&s, out);

        size_t take = n < SHA256_DIGEST ? n : SHA256_DIGEST;
        memcpy(p, out, take);
        p += take; n -= take;

        /* Ratchet: the pool moves on, so this output cannot be re-derived
         * from the pool's later state. */
        sha256_init(&s);
        sha256_update(&s, pool, sizeof(pool));
        sha256_update(&s, out, sizeof(out));
        sha256_final(&s, pool);
    }
}

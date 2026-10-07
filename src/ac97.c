/* ac97.c - Intel 82801AA AC97 audio, the card QEMU gives us with -device AC97.
 *
 * The PCI groundwork from the network card is most of what this needs: find
 * the device, turn on bus mastering, read two BARs. What is different is the
 * shape of the DMA. The RTL8139 takes one buffer per packet; AC97 reads a
 * BUFFER DESCRIPTOR LIST, an array of up to 32 entries each naming a block of
 * samples, and walks it on its own. You hand it a list, say which entry is the
 * last valid one, and set the run bit.
 *
 * Two things are easy to get wrong and silent when you do:
 *
 *   - A descriptor's length is a count of SAMPLES, not bytes and not frames.
 *     For 16-bit stereo one frame is two samples, so a buffer of N int16s is
 *     N samples however you like to think about it. Getting this wrong plays
 *     the right sound at the wrong speed, which is easy to mistake for a
 *     sample-rate problem.
 *   - The descriptor list and the sample buffers are read by the card, not by
 *     us, so they need physical addresses. The kernel is identity-mapped, so
 *     static storage works and a pointer IS the physical address - the same
 *     reason the RTL8139 driver can use static buffers.
 *
 * Volume on AC97 is attenuation: 0 is loudest and 0x8000 is mute, which reads
 * backwards from every other volume control.
 */
#include "ac97.h"
#include "pci.h"
#include "klog.h"
#include "str.h"
#include "task.h"

/* Mixer, BAR0 */
#define NAM_RESET          0x00
#define NAM_MASTER_VOL     0x02
#define NAM_PCM_VOL        0x18
#define NAM_EXT_AUDIO_ID   0x28
#define NAM_EXT_AUDIO_CTL  0x2A
#define NAM_PCM_DAC_RATE   0x2C

/* Bus master, BAR1. The PCM-out box starts at 0x10. */
#define NABM_PO_BDBAR      0x10
#define NABM_PO_CIV        0x14
#define NABM_PO_LVI        0x15
#define NABM_PO_SR         0x16
#define NABM_PO_PICB       0x18
#define NABM_PO_CR         0x1B
#define NABM_GLOB_CNT      0x2C
#define NABM_GLOB_STA      0x30

#define CR_RUN             0x01
#define CR_RESET           0x02
#define SR_DCH             0x01      /* DMA controller halted */

#define GLOB_CNT_COLD      0x02
#define GLOB_STA_PCR       0x100     /* primary codec ready */

static inline void  outb_(uint16_t p, uint8_t v)  { __asm__ volatile ("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void  outw_(uint16_t p, uint16_t v) { __asm__ volatile ("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline void  outl_(uint16_t p, uint32_t v) { __asm__ volatile ("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t  inb_(uint16_t p)  { uint8_t r;  __asm__ volatile ("inb %1,%0" : "=a"(r) : "Nd"(p)); return r; }
static inline uint16_t inw_(uint16_t p)  { uint16_t r; __asm__ volatile ("inw %1,%0" : "=a"(r) : "Nd"(p)); return r; }
static inline uint32_t inl_(uint16_t p)  { uint32_t r; __asm__ volatile ("inl %1,%0" : "=a"(r) : "Nd"(p)); return r; }

/* One descriptor: where the samples are, how many, and two flag bits. */
typedef struct {
    uint32_t addr;
    uint16_t samples;
    uint16_t flags;
} __attribute__((packed)) bdl_entry_t;

#define AC97_NBUF        8
#define AC97_BUF_SAMPLES 8192                    /* int16s, so 4096 stereo frames */

static bdl_entry_t bdl[32]                        __attribute__((aligned(8)));
static int16_t     pcm[AC97_NBUF][AC97_BUF_SAMPLES] __attribute__((aligned(4)));

static uint16_t nam, nabm;
static int      present;
static uint32_t rate = AC97_RATE;

int ac97_present(void) { return present; }
uint32_t ac97_rate(void) { return rate; }

/* Q15 sine, one period over 256 entries. Generated rather than computed: the
 * kernel has no math library and does not need one for this. */
static const int16_t sine_q15[256] = {
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
      6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
     12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
     23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
     27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
     32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
     32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
     30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
     27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
     18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
     12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
         0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,
};

/* One sample of the sine, for the mixer's tone voices. */
int16_t ac97_sine_sample(uint32_t phase) { return sine_q15[phase >> 24]; }

/* Phase accumulator whose FULL 32-bit range is one period, so the top 8 bits
 * are the table index and wrapping is free.
 *
 * The step is therefore hz * 2^32 / rate. Getting the shift wrong here is
 * quiet: 2^24 instead of 2^32 played a 1.7 Hz wave, which sounds like nothing
 * at all and measured as a signal with no zero crossings at full amplitude.
 * Dividing 0xFFFFFFFF by the rate first keeps every step 32-bit, so this needs
 * no 64-bit division helper from libgcc. */
int ac97_fill_tone(int16_t *out, uint32_t samples, uint32_t hz, uint32_t *phase) {
    if (!hz || !rate) return -1;
    uint32_t per_hz = 0xFFFFFFFFu / rate;         /* phase units per Hz, per frame */
    uint32_t step   = hz * per_hz;
    for (uint32_t i = 0; i + 1 < samples; i += 2) {
        int16_t s = sine_q15[*phase >> 24];      /* top 8 bits index the table */
        out[i]     = s;                     /* left  */
        out[i + 1] = s;                     /* right */
        *phase += step;
    }
    return 0;
}

static void mixer_write(uint8_t reg, uint16_t val) { outw_((uint16_t)(nam + reg), val); }
static uint16_t mixer_read(uint8_t reg)            { return inw_((uint16_t)(nam + reg)); }

int ac97_init(void) {
    pci_dev_t d;
    if (pci_find(0x8086, 0x2415, &d) < 0) return -1;

    pci_enable_bus_master(&d);
    nam  = (uint16_t)pci_bar(&d, 0);
    nabm = (uint16_t)pci_bar(&d, 1);
    if (!nam || !nabm) return -1;

    /* Release the cold reset and wait for the codec to say it is there. */
    outl_((uint16_t)(nabm + NABM_GLOB_CNT), GLOB_CNT_COLD);
    int ready = 0;
    for (int i = 0; i < 1000; i++) {
        if (inl_((uint16_t)(nabm + NABM_GLOB_STA)) & GLOB_STA_PCR) { ready = 1; break; }
        task_sleep(1);
    }
    if (!ready) { klog("[ac97] codec never reported ready\n"); return -1; }

    mixer_write(NAM_RESET, 0);
    mixer_write(NAM_MASTER_VOL, 0x0000);   /* attenuation, so 0 is loudest */
    mixer_write(NAM_PCM_VOL,    0x0000);

    /* Variable rate is optional. Without it the card is fixed at 48 kHz, so
     * ask, and believe the answer rather than assuming. */
    if (mixer_read(NAM_EXT_AUDIO_ID) & 0x0001) {
        mixer_write(NAM_EXT_AUDIO_CTL, mixer_read(NAM_EXT_AUDIO_CTL) | 0x0001);
        mixer_write(NAM_PCM_DAC_RATE, (uint16_t)AC97_RATE);
        rate = mixer_read(NAM_PCM_DAC_RATE);
        if (!rate) rate = AC97_RATE;
    } else {
        rate = 48000;
    }

    present = 1;
    klog("[ac97] ready, mixer at %x, bus master at %x, %u Hz\n", nam, nabm, rate);
    return 0;
}

int16_t *ac97_staging(uint32_t *max_samples) {
    if (max_samples) *max_samples = AC97_NBUF * AC97_BUF_SAMPLES;
    return (int16_t *)pcm;
}

int ac97_busy(void) {
    if (!present) return 0;
    return (inw_((uint16_t)(nabm + NABM_PO_SR)) & SR_DCH) ? 0 : 1;
}

/* Hand the card a run of buffers. `wait` polls until the DMA halts; without it
 * this returns as soon as the card is running, which is what a sound effect
 * during a game needs. */
static int ac97_run(int nbuf, uint32_t last_samples, int wait) {
    uint16_t cr = (uint16_t)(nabm + NABM_PO_CR);

    outb_((uint16_t)cr, CR_RESET);
    for (int i = 0; i < 1000 && (inb_((uint16_t)cr) & CR_RESET); i++) task_sleep(1);

    for (int i = 0; i < nbuf; i++) {
        bdl[i].addr    = (uint32_t)pcm[i];
        bdl[i].samples = (uint16_t)((i == nbuf - 1) ? last_samples : AC97_BUF_SAMPLES);
        bdl[i].flags   = 0;
    }
    outl_((uint16_t)(nabm + NABM_PO_BDBAR), (uint32_t)bdl);
    outb_((uint16_t)(nabm + NABM_PO_LVI), (uint8_t)(nbuf - 1));
    outw_((uint16_t)(nabm + NABM_PO_SR), 0x1C);        /* clear stale status */
    outb_((uint16_t)cr, CR_RUN);

    /* Without `wait` the channel is left running and we return at once. The
     * reset at the top of this function is what makes that safe to do twice:
     * a channel left running has its index past LVI, and starting it again
     * without the reset plays nothing at all. That write spent a while going
     * to port 0x1B instead of 0xC41B because of a truncating cast, which made
     * the first sound work and every later one silent. */
    if (!wait) return 0;

    /* Poll rather than take the interrupt: one sound at a time needs no ring,
     * and it keeps what is being tested honest. */
    uint32_t total_frames = ((uint32_t)(nbuf - 1) * AC97_BUF_SAMPLES + last_samples) / 2;
    uint32_t ms = (total_frames * 1000) / rate;
    for (uint32_t waited = 0; waited < ms + 500; waited += 10) {
        if (inw_((uint16_t)(nabm + NABM_PO_SR)) & SR_DCH) break;
        task_sleep(10);
    }
    outb_((uint16_t)cr, 0);
    return 0;
}

/* ── Cyclic ring, for the mixer ─────────────────────────────────────────────
 *
 * The one-shot path above hands the card a finite descriptor list and lets it
 * halt at the end. The ring reuses the same staging memory as 32 chunks of
 * 2048 samples (about 21 ms each) and never lets the card reach the last
 * valid index: the mixer task reads the card's position (CIV) and keeps LVI a
 * few chunks ahead, filling each chunk it advances over. Stop feeding it and
 * the card drains what is queued and halts on its own, which is the idle
 * state. No interrupt needed: the scheduler already wakes a task far more
 * often than a 21 ms chunk empties. */
#define RING_CHUNKS        32
#define RING_CHUNK_SAMPLES ((AC97_NBUF * AC97_BUF_SAMPLES) / RING_CHUNKS)

int16_t *ac97_ring_chunk(int i) {
    return (int16_t *)pcm + (uint32_t)(i % RING_CHUNKS) * RING_CHUNK_SAMPLES;
}

uint32_t ac97_ring_chunk_samples(void) { return RING_CHUNK_SAMPLES; }

/* Start the ring with `prefilled` chunks already mixed into chunks 0.. */
int ac97_ring_start(int prefilled) {
    if (!present || prefilled < 1) return -1;
    uint16_t cr = (uint16_t)(nabm + NABM_PO_CR);

    outb_(cr, CR_RESET);
    for (int i = 0; i < 1000 && (inb_(cr) & CR_RESET); i++) task_sleep(1);

    for (int i = 0; i < RING_CHUNKS; i++) {
        bdl[i].addr    = (uint32_t)ac97_ring_chunk(i);
        bdl[i].samples = (uint16_t)RING_CHUNK_SAMPLES;
        bdl[i].flags   = 0;
    }
    outl_((uint16_t)(nabm + NABM_PO_BDBAR), (uint32_t)bdl);
    outb_((uint16_t)(nabm + NABM_PO_LVI), (uint8_t)(prefilled - 1));
    outw_((uint16_t)(nabm + NABM_PO_SR), 0x1C);
    outb_(cr, CR_RUN);
    return 0;
}

int  ac97_ring_civ(void) { return inb_((uint16_t)(nabm + NABM_PO_CIV)); }
void ac97_ring_set_lvi(int lvi) {
    outb_((uint16_t)(nabm + NABM_PO_LVI), (uint8_t)(lvi % RING_CHUNKS));
}
int  ac97_ring_halted(void) {
    return (inw_((uint16_t)(nabm + NABM_PO_SR)) & SR_DCH) ? 1 : 0;
}
void ac97_ring_stop(void) {
    outb_((uint16_t)(nabm + NABM_PO_CR), 0);
}

/* Play `samples` int16s already sitting in the staging area. */
int ac97_play(uint32_t samples, int wait) {
    if (!present || !samples) return -1;
    uint32_t cap = AC97_NBUF * AC97_BUF_SAMPLES;
    if (samples > cap) samples = cap;
    samples &= ~1u;                                  /* whole frames */

    int nbuf = (int)((samples + AC97_BUF_SAMPLES - 1) / AC97_BUF_SAMPLES);
    uint32_t last = samples - (uint32_t)(nbuf - 1) * AC97_BUF_SAMPLES;
    return ac97_run(nbuf, last, wait);
}

int ac97_tone(uint32_t hz, uint32_t ms) {
    if (!present) return -1;
    if (ms > AC97_TONE_MAX_MS) ms = AC97_TONE_MAX_MS;

    uint32_t frames  = (rate * ms) / 1000;
    uint32_t samples = frames * 2;                    /* stereo */
    if (samples == 0) return -1;

    uint32_t phase = 0;
    int nbuf = 0;
    uint32_t left = samples, last = 0;
    while (left && nbuf < AC97_NBUF) {
        uint32_t take = left > AC97_BUF_SAMPLES ? AC97_BUF_SAMPLES : left;
        take &= ~1u;                                  /* whole frames only */
        if (!take) break;
        ac97_fill_tone(pcm[nbuf], take, hz, &phase);
        last = take;
        left -= take;
        nbuf++;
    }
    if (!nbuf) return -1;

    klog("[ac97] tone %u Hz for %u ms, %d buffer%s\n", hz, ms, nbuf, nbuf == 1 ? "" : "s");
    return ac97_run(nbuf, last, 1);
}

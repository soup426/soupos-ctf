#pragma once
#include <stdint.h>

/* Intel 82801AA AC97 audio (QEMU: -device AC97). 16-bit signed stereo.
 *
 * Volume on this card is attenuation: 0 is loudest, 0x8000 is mute.
 * A descriptor's length is a count of SAMPLES, not bytes and not frames. */

#define AC97_RATE         48000u
#define AC97_TONE_MAX_MS  600u     /* what the static buffers hold */

int      ac97_init(void);
int      ac97_present(void);
uint32_t ac97_rate(void);

/* Play a sine at `hz` for `ms`, blocking until it has finished. */
int      ac97_tone(uint32_t hz, uint32_t ms);

/* Fill `samples` int16s of interleaved stereo sine into `out`, carrying the
 * phase accumulator across calls. Exposed for tests. */
int      ac97_fill_tone(int16_t *out, uint32_t samples, uint32_t hz, uint32_t *phase);
int16_t  ac97_sine_sample(uint32_t phase);

/* The staging area the card plays from: write interleaved 16-bit stereo here,
 * then call ac97_play. One sound at a time, which is what the hardware's
 * single PCM-out channel gives us without mixing in software. */
int16_t *ac97_staging(uint32_t *max_samples);

/* Play `samples` int16s from the staging area. `wait` blocks until the card
 * has finished; without it this returns immediately, which is what a sound
 * effect fired mid-game needs. */
int      ac97_play(uint32_t samples, int wait);

/* Is the card still playing? */
int      ac97_busy(void);

/* The cyclic ring the software mixer feeds: 32 chunks of about 21 ms over the
 * same staging memory. The mixer keeps LVI ahead of CIV; when it stops, the
 * card drains and halts on its own. See mixer.c for the only caller. */
int16_t *ac97_ring_chunk(int i);
uint32_t ac97_ring_chunk_samples(void);
int      ac97_ring_start(int prefilled);
int      ac97_ring_civ(void);
void     ac97_ring_set_lvi(int lvi);
int      ac97_ring_halted(void);
void     ac97_ring_stop(void);

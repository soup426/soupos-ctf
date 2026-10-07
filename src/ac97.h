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
 * 8.24 phase accumulator across calls. Exposed for tests. */
int      ac97_fill_tone(int16_t *out, uint32_t samples, uint32_t hz, uint32_t *phase);

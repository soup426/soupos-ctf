#pragma once
#include <stdint.h>

/* The software mixer: up to four sounds at once, summed with saturation into
 * the AC97 ring. Voices return immediately; the mixer task does the work.
 * A fifth sound steals the oldest voice. */
int mixer_start(void);                /* spawn the task; needs ac97 + tasks */
int mixer_active(void);
int mixer_play_raw(const uint8_t *samples, uint32_t count, uint32_t src_rate);
int mixer_tone(uint32_t hz, uint32_t ms);

#pragma once
#include <stdint.h>

/* The software mixer: up to four sounds at once, summed with saturation into
 * the AC97 ring. Voices return immediately; the mixer task does the work.
 * A fifth sound steals the oldest voice. */
int mixer_start(void);                /* spawn the task; needs ac97 + tasks */
int mixer_active(void);
int mixer_play_raw(const uint8_t *samples, uint32_t count, uint32_t src_rate);
int mixer_tone(uint32_t hz, uint32_t ms);

/* Master level, 0-100, applied to the summed output before it saturates - so
 * turning it down removes clipping rather than scaling an already-clipped
 * signal, and the balance between music and effects does not shift. */
int  mixer_master(void);
void mixer_set_master(int level);

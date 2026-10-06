#pragma once
#include <stdint.h>

/* PC speaker via PIT channel 2.
 * Beeps at `freq` Hz for `ms` milliseconds, then goes silent.
 * freq=0 or ms=0 -> silence immediately.
 */
void speaker_beep(uint32_t freq, uint32_t ms);
void speaker_off(void);

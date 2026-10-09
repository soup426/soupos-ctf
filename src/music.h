#pragma once
#include <stdint.h>

/* Doom's MUS music, synthesised as square waves.
 *
 * Not a mixer voice: sixteen channels do not fit in four, and music runs
 * continuously and must not be stolen by a sound effect. The mixer asks for
 * one sample per frame and adds it, so all four voices stay free for effects.
 *
 * Channel 15 (percussion) is skipped, and pitch bend and the instrument list
 * are read and ignored - the tune, rhythm and harmony come through, the OPL2
 * timbres do not. */

int     music_play(const char *lump_name, int loop);
void    music_stop(void);
int     music_playing(void);

/* One frame, summed across channels. Zero when nothing is playing. */
int16_t music_sample(void);

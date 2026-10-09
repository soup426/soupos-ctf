#pragma once

/* Play a Doom sound effect lump (DMX format, e.g. "DSPISTOL") through the
 * AC97 card. Returns 0 if the card started playing, -1 otherwise. Does not
 * block: the card plays while the game carries on.
 *
 * Needs a WAD already open (wad_init). One sound at a time, because the
 * hardware has one PCM-out channel and nothing here mixes. */
int doomsnd_play(const char *lump);

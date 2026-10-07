/* doomsnd.c - Doom's sound effects through the AC97 card.
 *
 * A DS* lump is the DMX format: an eight-byte header of
 *
 *     uint16 format (always 3)
 *     uint16 sample rate (11025 in every lump of DOOM1.WAD)
 *     uint32 sample count
 *
 * followed by that many UNSIGNED eight-bit samples, centred on 128. The card
 * wants signed sixteen-bit stereo at its own rate, so each sample needs three
 * things done to it: recentre, widen, and resample.
 *
 * Resampling is nearest-neighbour with a fixed-point source cursor. For
 * 11025 Hz into 48000 Hz that repeats each input sample four or five times,
 * and on eight-bit effects recorded in 1993 the difference between that and
 * an interpolating resampler is not audible. What IS audible is getting the
 * ratio backwards, which plays the sound at a quarter speed and sounds like a
 * different weapon entirely, so the arithmetic is written out below rather
 * than compressed.
 */
#include "doomsnd.h"
#include "ac97.h"
#include "wad.h"
#include "klog.h"
#include "str.h"

/* The largest DS lump in DOOM1.WAD is about 32 KB. */
#define DS_MAX 65536
static uint8_t ds_raw[DS_MAX];

int doomsnd_play(const char *lump) {
    if (!ac97_present()) return -1;

    int idx = wad_find_lump(lump);
    if (idx < 0) { klog("[doomsnd] no lump %s\n", lump); return -1; }

    int got = wad_read_lump(idx, ds_raw, DS_MAX);
    if (got < 12) { klog("[doomsnd] %s too short (%d bytes)\n", lump, got); return -1; }

    uint16_t fmt   = (uint16_t)(ds_raw[0] | (ds_raw[1] << 8));
    uint16_t srate = (uint16_t)(ds_raw[2] | (ds_raw[3] << 8));
    uint32_t count = (uint32_t)ds_raw[4] | ((uint32_t)ds_raw[5] << 8) |
                     ((uint32_t)ds_raw[6] << 16) | ((uint32_t)ds_raw[7] << 24);

    if (fmt != 3 || !srate) { klog("[doomsnd] %s is not DMX format 3\n", lump); return -1; }
    if (count > (uint32_t)got - 8) count = (uint32_t)got - 8;   /* trust the lump size */

    const uint8_t *src = ds_raw + 8;
    uint32_t dst_rate  = ac97_rate();

    uint32_t max = 0;
    int16_t *out = ac97_staging(&max);

    /* Output frames, from input samples: count * dst_rate / srate. Done in
     * two steps so a long lump cannot overflow a 32-bit multiply. */
    uint32_t frames = (count / srate) * dst_rate + ((count % srate) * dst_rate) / srate;
    if (frames * 2 > max) frames = max / 2;

    /* Source cursor in Q16 input-samples per output frame. */
    uint32_t step = (uint32_t)(((uint64_t)srate << 16) / dst_rate);
    uint32_t cur  = 0;

    for (uint32_t i = 0; i < frames; i++) {
        uint32_t si = cur >> 16;
        if (si >= count) si = count - 1;
        /* 128 is silence in the lump; widen to fill the 16-bit range. */
        int16_t s = (int16_t)(((int)src[si] - 128) << 8);
        out[i * 2]     = s;
        out[i * 2 + 1] = s;
        cur += step;
    }

    klog("[doomsnd] %s: %u samples at %u Hz -> %u frames at %u Hz\n",
         lump, count, srate, frames, dst_rate);
    return ac97_play(frames * 2, 0);
}

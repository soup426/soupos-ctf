/* music.c - Doom's MUS lumps, played as square waves.
 *
 * A D_* lump is MUS: a header, a list of instrument patches, then a score of
 * events on sixteen channels. The events that occur in practice are play,
 * release, pitch bend, controller and end - exactly five - and the score is
 * timed in ticks of 140 Hz with a variable-length delay after any event whose
 * top bit is set.
 *
 * The synthesis is deliberately plain: one square wave per channel at the
 * note's frequency, scaled by the channel volume. Doom's music was written for
 * an OPL2, and emulating one is a project rather than a feature; square waves
 * get the tune, the rhythm and the harmony, which is what makes a level feel
 * like that level. The instrument list is read and ignored, and said to be.
 *
 * WHY THIS IS NOT A MIXER VOICE. Sixteen channels into four voices does not
 * fit, and music is not a sound effect: it runs continuously, it loops, and it
 * must not be stolen when three doors open at once. So the mixer asks this
 * module for one sample per frame and adds it, which leaves all four voices
 * free for effects and keeps the music's own channel count a private matter.
 */
#include "music.h"
#include "ac97.h"
#include "wad.h"
#include "klog.h"
#include "str.h"

#define MUS_MAX      65536      /* the largest D_* lump in DOOM1.WAD is ~37 KB */
#define MUS_CHANNELS 16
#define MUS_HZ       140        /* the score's tick rate, fixed by the format */

/* Channel 15 is percussion, where the note number selects a drum rather than
 * a pitch. A square wave at that "pitch" is noise in the wrong sense, so it
 * is skipped: no drums is better than wrong drums. */
#define PERCUSSION_CHANNEL 15

static const uint16_t note_hz[128] = {
        8,     9,     9,    10,    10,    11,    12,    12,
       13,    14,    15,    15,    16,    17,    18,    19,
       21,    22,    23,    24,    26,    28,    29,    31,
       33,    35,    37,    39,    41,    44,    46,    49,
       52,    55,    58,    62,    65,    69,    73,    78,
       82,    87,    92,    98,   104,   110,   117,   123,
      131,   139,   147,   156,   165,   175,   185,   196,
      208,   220,   233,   247,   262,   277,   294,   311,
      330,   349,   370,   392,   415,   440,   466,   494,
      523,   554,   587,   622,   659,   698,   740,   784,
      831,   880,   932,   988,  1047,  1109,  1175,  1245,
     1319,  1397,  1480,  1568,  1661,  1760,  1865,  1976,
     2093,  2217,  2349,  2489,  2637,  2794,  2960,  3136,
     3322,  3520,  3729,  3951,  4186,  4435,  4699,  4978,
     5274,  5588,  5920,  6272,  6645,  7040,  7459,  7902,
     8372,  8870,  9397,  9956, 10548, 11175, 11840, 12544,
};

static uint8_t  lump[MUS_MAX];
static uint32_t score_at, score_end, pos;
static int      playing, looping;

typedef struct {
    uint8_t  note;              /* 0 = silent, else the MIDI note + 1 */
    uint8_t  vol;               /* 0-127                              */
    uint32_t phase, step;
} chan_t;
static chan_t chans[MUS_CHANNELS];

static uint32_t tick_acc, frames_per_tick;   /* both Q16 */
static uint32_t delay_ticks;

static void all_off(void) {
    for (int i = 0; i < MUS_CHANNELS; i++) { chans[i].note = 0; chans[i].phase = 0; }
}

int music_playing(void) { return playing; }

void music_stop(void) {
    playing = 0;
    all_off();
}

int music_play(const char *lump_name, int loop) {
    if (!ac97_present()) return -1;

    int idx = wad_find_lump(lump_name);
    if (idx < 0) { klog("[music] no lump %s\n", lump_name); return -1; }

    int got = wad_read_lump(idx, lump, MUS_MAX);
    if (got < 16) { klog("[music] %s too short\n", lump_name); return -1; }

    if (lump[0] != 'M' || lump[1] != 'U' || lump[2] != 'S' || lump[3] != 0x1A) {
        klog("[music] %s is not a MUS lump\n", lump_name);
        return -1;
    }
    uint32_t slen  = (uint32_t)(lump[4]  | (lump[5]  << 8));
    uint32_t start = (uint32_t)(lump[6]  | (lump[7]  << 8));
    uint32_t pri   = (uint32_t)(lump[8]  | (lump[9]  << 8));
    uint32_t ninst = (uint32_t)(lump[12] | (lump[13] << 8));

    if (start + slen > (uint32_t)got) slen = (uint32_t)got - start;

    all_off();
    score_at  = start;
    score_end = start + slen;
    pos       = start;
    delay_ticks = 0;
    tick_acc    = 0;
    frames_per_tick = (ac97_rate() << 16) / MUS_HZ;
    looping   = loop;
    playing   = 1;

    klog("[music] %s: %u bytes of score, %u channels, %u instruments (ignored)\n",
         lump_name, slen, pri, ninst);
    return 0;
}

static void note_on(int ch, uint8_t note, uint8_t vol) {
    if (ch == PERCUSSION_CHANNEL || note > 127) return;
    chans[ch].note = (uint8_t)(note + 1);
    chans[ch].vol  = vol;
    chans[ch].step = (uint32_t)note_hz[note] * (0xFFFFFFFFu / ac97_rate());
}

/* One score tick: run events until one of them carries a delay. */
static void music_tick(void) {
    if (delay_ticks) { delay_ticks--; return; }

    for (;;) {
        if (pos >= score_end) {
            if (!looping) { music_stop(); return; }
            pos = score_at;                 /* round again */
            all_off();
            return;
        }
        uint8_t b  = lump[pos++];
        int     ch = b & 0x0F;
        int     ty = (b >> 4) & 7;
        int     last = (b & 0x80) != 0;

        switch (ty) {
            case 0:                                   /* release */
                if (pos < score_end) { pos++; chans[ch].note = 0; }
                break;
            case 1: {                                 /* play */
                if (pos >= score_end) break;
                uint8_t nv = lump[pos++];
                uint8_t vol = chans[ch].vol ? chans[ch].vol : 100;
                if (nv & 0x80) { if (pos < score_end) vol = lump[pos++] & 0x7F; }
                note_on(ch, (uint8_t)(nv & 0x7F), vol);
                break;
            }
            case 2: pos++; break;                     /* pitch bend: ignored */
            case 3: pos++; break;                     /* system event        */
            case 4:                                   /* controller */
                if (pos + 1 < score_end) {
                    uint8_t ctl = lump[pos++], val = lump[pos++];
                    if (ctl == 3) chans[ch].vol = (uint8_t)(val & 0x7F);  /* volume */
                } else pos = score_end;
                break;
            case 6:                                   /* score end */
                if (!looping) { music_stop(); return; }
                pos = score_at;
                all_off();
                return;
            default: break;
        }

        if (last) {
            /* A variable-length delay, seven bits at a time, high bit = more. */
            uint32_t d = 0;
            while (pos < score_end) {
                uint8_t v = lump[pos++];
                d = (d << 7) | (uint32_t)(v & 0x7F);
                if (!(v & 0x80)) break;
            }
            delay_ticks = d;
            return;
        }
    }
}

/* One frame of music, summed across channels. Called by the mixer. */
int16_t music_sample(void) {
    if (!playing) return 0;

    tick_acc += 1u << 16;
    while (tick_acc >= frames_per_tick) {
        tick_acc -= frames_per_tick;
        music_tick();
        if (!playing) return 0;
    }

    int32_t acc = 0;
    for (int i = 0; i < MUS_CHANNELS; i++) {
        if (!chans[i].note) continue;
        chans[i].phase += chans[i].step;
        /* A square wave: the top bit of the phase is the sign. The scale
         * leaves room for several channels plus sound effects on top without
         * the sum clipping, which matters because the mixer saturates. */
        int32_t amp = (int32_t)chans[i].vol * 20;
        acc += (chans[i].phase & 0x80000000u) ? amp : -amp;
    }
    if (acc >  32767) acc =  32767;
    if (acc < -32768) acc = -32768;
    return (int16_t)acc;
}

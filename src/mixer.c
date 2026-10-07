/* mixer.c - several sounds at once, on hardware with one PCM channel.
 *
 * The AC97 plays exactly one stream, so overlapping sounds are summed in
 * software. Four voices feed one ring. A voice is either a Doom lump - kept
 * in its RAW form, unsigned 8-bit at the lump's own rate, resampled on the
 * way out - or a generated tone, which is a phase accumulator and no storage
 * at all. Keeping lumps raw makes a voice slot 64 KB instead of 200, and
 * resampling four voices per frame is nothing next to the cost of writing
 * the output.
 *
 * The ring is topped up by a task, not an interrupt. The card's read position
 * advances one 21 ms chunk at a time; the task wakes every 10 ms and keeps a
 * three-chunk lead. Stop feeding it and the card drains what is queued and
 * halts, which is the idle state; the task then just sleeps a little longer
 * between looks. (A wait queue would use less of nothing - the poll is 20 Hz
 * against a 100 Hz scheduler tick - and a blocked task plus a wake from the
 * caller is a lost-wakeup race waiting for a careless edit.)
 *
 * Summing goes through a 32-bit accumulator and saturates. Two full-scale
 * sounds clip - audibly, on purpose, like every 16-bit mixer since the
 * nineties - rather than wrapping, which sounds like the machine dying.
 */
#include "mixer.h"
#include "ac97.h"
#include "task.h"
#include "klog.h"
#ifndef NO_DOOM
#include "music.h"
#else
/* Music lives in the WAD reader, which a DOOM=0 build does not have. The
 * mixer still works; there is simply nothing to add. */
static inline int     music_playing(void) { return 0; }
static inline int16_t music_sample(void)  { return 0; }
#endif
#include "str.h"

#define NVOICES    4
#define VOICE_RAW  65536            /* the largest DS lump is about 32 KB */

typedef enum { V_FREE = 0, V_LUMP, V_TONE } vkind_t;

typedef struct {
    volatile vkind_t kind;
    /* lump voices */
    uint8_t  raw[VOICE_RAW];
    uint32_t raw_len;
    uint32_t pos;                   /* Q16 cursor into raw[]          */
    uint32_t step;                  /* Q16 source samples per frame   */
    /* tone voices */
    uint32_t phase, phase_step;     /* full 32-bit range = one period */
    uint32_t frames_left;
    uint32_t seq;                   /* age, for stealing the oldest   */
} voice_t;

static voice_t  voices[NVOICES];

/* 0-100, applied to the SUM rather than to each voice, which is what makes it
 * a volume control rather than a per-sound one: the balance between music and
 * effects stays put as it moves. Applied before the saturation, so turning it
 * down also removes clipping instead of just scaling a clipped signal. */
static int master_level = 100;
static uint32_t next_seq;
static int      ring_live;
static int      fill_next;
static int      started;

static int any_active(void) {
    /* Music counts: it is not a voice, but the ring has to keep running while
     * it plays or the card drains and halts mid-tune. */
    if (music_playing()) return 1;
    for (int i = 0; i < NVOICES; i++)
        if (voices[i].kind != V_FREE) return 1;
    return 0;
}

static void mix_chunk(int16_t *out, uint32_t samples) {
    uint32_t frames = samples / 2;
    for (uint32_t f = 0; f < frames; f++) {
        int32_t acc = music_sample();     /* zero when nothing is playing */
        for (int v = 0; v < NVOICES; v++) {
            voice_t *vo = &voices[v];
            if (vo->kind == V_LUMP) {
                uint32_t si = vo->pos >> 16;
                if (si >= vo->raw_len) { vo->kind = V_FREE; continue; }
                acc += (int32_t)(((int)vo->raw[si] - 128) << 8);
                vo->pos += vo->step;
            } else if (vo->kind == V_TONE) {
                if (!vo->frames_left) { vo->kind = V_FREE; continue; }
                acc += ac97_sine_sample(vo->phase);
                vo->phase += vo->phase_step;
                vo->frames_left--;
            }
        }
        if (master_level != 100) acc = (acc * master_level) / 100;
        if (acc >  32767) acc =  32767;
        if (acc < -32768) acc = -32768;
        out[f * 2]     = (int16_t)acc;
        out[f * 2 + 1] = (int16_t)acc;
    }
}

static void mixer_task(void *arg) {
    (void)arg;
    task_set_service();
    for (;;) {
        if (!any_active()) {
            if (ring_live && ac97_ring_halted()) {
                ac97_ring_stop();
                ring_live = 0;
            }
            task_sleep(ring_live ? 10 : 50);
            continue;
        }
        if (!ring_live) {
            mix_chunk(ac97_ring_chunk(0), ac97_ring_chunk_samples());
            mix_chunk(ac97_ring_chunk(1), ac97_ring_chunk_samples());
            fill_next = 2;
            if (ac97_ring_start(2) == 0) ring_live = 1;
        } else {
            int civ  = ac97_ring_civ();
            int lead = (fill_next - civ + 32) % 32;
            while (lead < 3) {
                mix_chunk(ac97_ring_chunk(fill_next), ac97_ring_chunk_samples());
                ac97_ring_set_lvi(fill_next);
                fill_next = (fill_next + 1) % 32;
                lead++;
            }
        }
        task_sleep(10);
    }
}

static voice_t *grab_voice(void) {
    for (int i = 0; i < NVOICES; i++)
        if (voices[i].kind == V_FREE) return &voices[i];
    voice_t *oldest = &voices[0];
    for (int i = 1; i < NVOICES; i++)
        if (voices[i].seq < oldest->seq) oldest = &voices[i];
    oldest->kind = V_FREE;          /* steal: the mixer skips it next frame */
    return oldest;
}

int mixer_start(void) {
    if (!ac97_present()) return -1;
    if (started) return 0;
    if (!task_spawn("mixer", mixer_task, 0)) return -1;
    started = 1;
    klog("[mixer] %d voices over a %u-sample ring\n",
         NVOICES, 32 * ac97_ring_chunk_samples());
    return 0;
}

int mixer_active(void) { return started; }

int mixer_play_raw(const uint8_t *samples, uint32_t count, uint32_t src_rate) {
    if (!started || !count || !src_rate) return -1;
    if (count > VOICE_RAW) count = VOICE_RAW;
    voice_t *v = grab_voice();
    memcpy(v->raw, samples, count);
    v->raw_len = count;
    v->pos     = 0;
    v->step    = (uint32_t)(((uint64_t)src_rate << 16) / ac97_rate());
    v->seq     = next_seq++;
    v->kind    = V_LUMP;            /* last: this is what arms the voice */
    return 0;
}

int mixer_tone(uint32_t hz, uint32_t ms) {
    if (!started || !hz) return -1;
    voice_t *v = grab_voice();
    v->phase       = 0;
    v->phase_step  = hz * (0xFFFFFFFFu / ac97_rate());
    v->frames_left = (ac97_rate() / 1000) * ms;
    v->seq         = next_seq++;
    v->kind        = V_TONE;
    klog("[mixer] tone %u Hz for %u ms\n", hz, ms);
    return 0;
}

int  mixer_master(void) { return master_level; }

void mixer_set_master(int level) {
    if (level < 0)   level = 0;
    if (level > 100) level = 100;
    master_level = level;
    klog("[mixer] master level %d\n", level);
}

/* clip.c - the clipboard. See clip.h for what it is for.
 *
 * Deliberately fixed buffers rather than heap allocations: it is touched
 * from the shell's line editor on every Ctrl+U, and a static costs less
 * than the bookkeeping of growing one. Nothing here can fail, which keeps the
 * callers simple.
 *
 * One per cook (v0.60.13). It used to be one for the whole kernel, so text a
 * cook cut over SSH could be pasted by whoever sat at the console. Keyed by
 * the cook rather than the terminal: `$(scraps)` runs on a capture terminal
 * of its own and must still see the cook's clipboard, and a cook's two
 * sessions sharing one is what a person would expect.
 */
#include "clip.h"
#include "str.h"
#include "klog.h"
#include "users.h"

static char     clip_buf[USERS_MAX][CLIP_MAX];
static uint32_t clip_n[USERS_MAX];

static int me(void) {
    uint8_t u = users_current_uid();
    return u < USERS_MAX ? u : 0;
}

void clip_set(const char *data, uint32_t len) {
    int u = me();
    if (!data || len == 0) { clip_n[u] = 0; return; }
    if (len > CLIP_MAX) len = CLIP_MAX;      /* keep the head, drop the rest */
    memcpy(clip_buf[u], data, len);
    clip_n[u] = len;
    klog("[clip] set %u bytes\n", clip_n[u]);
}

uint32_t clip_get(char *out, uint32_t max) {
    if (!out || max == 0) return 0;
    int u = me();
    uint32_t n = (clip_n[u] < max) ? clip_n[u] : max;
    memcpy(out, clip_buf[u], n);
    return n;
}

uint32_t    clip_len(void)  { return clip_n[me()]; }
const char *clip_peek(void) { return clip_buf[me()]; }

void clip_clear(void) {
    int u = me();
    memset(clip_buf[u], 0, clip_n[u]);
    clip_n[u] = 0;
}

void clip_forget(uint8_t uid) {
    if (uid >= USERS_MAX) return;
    memset(clip_buf[uid], 0, CLIP_MAX);
    clip_n[uid] = 0;
}

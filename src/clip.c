/* clip.c - the system clipboard. See clip.h for what it is for.
 *
 * Deliberately a fixed buffer rather than a heap allocation: it is touched
 * from the shell's line editor on every Ctrl+U, and a 4 KB static costs less
 * than the bookkeeping of growing one. Nothing here can fail, which keeps the
 * callers simple.
 */
#include "clip.h"
#include "str.h"
#include "klog.h"

static char     clip_buf[CLIP_MAX];
static uint32_t clip_n;

void clip_set(const char *data, uint32_t len) {
    if (!data || len == 0) { clip_n = 0; return; }
    if (len > CLIP_MAX) len = CLIP_MAX;      /* keep the head, drop the rest */
    memcpy(clip_buf, data, len);
    clip_n = len;
    klog("[clip] set %u bytes\n", clip_n);
}

uint32_t clip_get(char *out, uint32_t max) {
    if (!out || max == 0) return 0;
    uint32_t n = (clip_n < max) ? clip_n : max;
    memcpy(out, clip_buf, n);
    return n;
}

uint32_t    clip_len(void)  { return clip_n; }
const char *clip_peek(void) { return clip_buf; }
void        clip_clear(void) { clip_n = 0; }

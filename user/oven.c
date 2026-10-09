/* oven.c - a kitchen timer in a window of its own (v0.60.164): the oven's
 * clock. The shell's eggtimer counts down in the terminal; this one has a
 * window.
 *
 *   cook oven.elf 90        a minute and a half (seconds, 5999 at most)
 *
 * MM:SS in seven-segment digits it draws itself (a program has no font),
 * counted down by the 100 Hz ticks, not by naps, so it does not drift. At
 * zero the window goes red, the mixer beeps (SYS_TONE; no sound card, no
 * beep) and it says "oven: done"; q or the close box ends it, early or
 * after ("oven: stopped at MM:SS").
 */
#include "ulib.h"

#define OW 240
#define OH 100
static unsigned px[OW * OH];

static void rect(int x, int y, int w, int h, unsigned c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) if (i >= 0 && j >= 0 && i < OW && j < OH) px[j * OW + i] = c;
}
/* Segments a-g, as on a calculator: a top, b top right, c bottom right,
 * d bottom, e bottom left, f top left, g middle. */
static const unsigned char seg[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
static void digit(int x, int y, int d, unsigned on) {
    const int w = 30, h = 60, t = 6;
    unsigned s = seg[d];
    if (s & 0x01) rect(x + t, y, w - 2 * t, t, on);                     /* a */
    if (s & 0x02) rect(x + w - t, y + t, t, h / 2 - t, on);             /* b */
    if (s & 0x04) rect(x + w - t, y + h / 2, t, h / 2 - t, on);         /* c */
    if (s & 0x08) rect(x + t, y + h - t, w - 2 * t, t, on);             /* d */
    if (s & 0x10) rect(x, y + h / 2, t, h / 2 - t, on);                 /* e */
    if (s & 0x20) rect(x, y + t, t, h / 2 - t, on);                     /* f */
    if (s & 0x40) rect(x + t, y + h / 2 - t / 2, w - 2 * t, t, on);     /* g */
}
static void draw(int id, int left, int done) {
    unsigned bg = done ? 0x00B03028 : 0x00202428, fg = done ? 0x00FFFFFF : 0x00F0B040;
    rect(0, 0, OW, OH, bg);
    int m = left / 60, s = left % 60;
    digit(28, 20, m / 10, fg); digit(68, 20, m % 10, fg);
    rect(112, 38, 6, 6, fg); rect(112, 62, 6, 6, fg);                   /* the colon */
    digit(130, 20, s / 10, fg); digit(170, 20, s % 10, fg);
    sys_win_put(id, px);
}
static void mmss(int left) {
    char b[6] = { (char)('0' + left / 600), (char)('0' + left / 60 % 10), ':', (char)('0' + left % 60 / 10), (char)('0' + left % 10), 0 };
    print(b);
}

int main(void) {
    char *argv[2];
    int secs = 0;
    if (uargv(argv, 2) != 1) { print("usage: oven.elf SECONDS\n"); return 2; }
    for (const char *p = argv[0]; *p; p++) {
        if (*p < '0' || *p > '9') { print("usage: oven.elf SECONDS\n"); return 2; }
        secs = secs * 10 + (*p - '0');
        if (secs > 5999) { print("oven: 5999 seconds at most\n"); return 2; }
    }
    int id = sys_win_open(OW, OH, "oven");
    if (id < 0) { print("oven: it needs the desktop (run countertop, then oven in one of its terminals)\n"); return 1; }
    unsigned end = sys_ticks() + (unsigned)secs * 100;
    int shown = -1, done = 0;
    for (;;) {
        unsigned now = sys_ticks();
        int left = now >= end ? 0 : (int)((end - now + 99) / 100);
        if (left != shown) { shown = left; draw(id, left, 0); }
        if (!left && !done) {
            done = 1;
            draw(id, 0, 1);
            sys_tone(880, 600);
            print("oven: done\n");
        }
        uwinev_t e;
        int r = sys_win_event(id, &e, done ? 1000 : 100);
        if (r < 0) return 0;
        if (r == 1 && (e.type == WEV_CLOSE || (e.type == WEV_KEY && e.key == 'q'))) {
            if (!done) { print("oven: stopped at "); mmss(shown); print("\n"); }
            sys_win_close(id);
            return 0;
        }
    }
}

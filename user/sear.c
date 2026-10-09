/* sear.c - how fast a program can draw into its window (v0.60.152): fast,
 * at high heat. A measurement, not a toy.
 *
 *   cook sear.elf W H N      put N frames of WxH, each a new colour
 *
 * Times its own work with rdtsc, calibrated against the 100 Hz ticks: the
 * fill (ring 3 writing W*H pixels) and the put (SYS_WIN_PUT copying them
 * into the kernel) per frame, and the frames a second the two allow. What
 * the desktop then shows of them it says itself when the window closes
 * ("[countertop] sear: N puts, M drawn, ..."). Prints one line:
 *   [sear] WxH: N frames, fill F us, put P us, R frames/s from here
 */
#include "ulib.h"

static unsigned long long tsc(void) { unsigned lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((unsigned long long)hi << 32) | lo; }
static int num(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }
static void line(const char *a, unsigned v) { print(a); print_int((int)v); }

int main(void) {
    char *argv[4];
    if (uargv(argv, 4) != 3) { print("usage: sear.elf W H N\n"); return 2; }
    int w = num(argv[0]), h = num(argv[1]), n = num(argv[2]);
    int id = sys_win_open(w, h, "sear");
    if (id < 0) { print("sear: no window (the desktop, and 640x480 at most)\n"); return 1; }
    unsigned *px = malloc((unsigned)(w * h * 4));
    for (int i = 0; i < w * h; i++) px[i] = 0;                /* every page touched before timing */
    unsigned t0 = sys_ticks();
    while (sys_ticks() == t0) ;
    unsigned k0 = sys_ticks();
    unsigned long long c0 = tsc(), fill = 0, put = 0;
    for (int f = 0; f < n; f++) {
        unsigned long long a = tsc();
        unsigned c = (unsigned)(f * 0x00071F3B) & 0x00FFFFFF;
        for (int i = 0; i < w * h; i++) px[i] = c;
        unsigned long long b = tsc();
        sys_win_put(id, px);
        put += tsc() - b;
        fill += b - a;
    }
    unsigned long long c1 = tsc();
    unsigned k1 = sys_ticks();
    /* cycles per microsecond from the ticks (10 ms each) the run took */
    unsigned long long per_us = k1 > k0 ? (c1 - c0) / ((unsigned long long)(k1 - k0) * 10000) : 1;
    if (!per_us) per_us = 1;
    unsigned fus = (unsigned)(fill / n / per_us), pus = (unsigned)(put / n / per_us);
    line("[sear] ", (unsigned)w); line("x", (unsigned)h); line(": ", (unsigned)n);
    line(" frames, fill ", fus); line(" us, put ", pus);
    line(" us, ", fus + pus ? 1000000u / (fus + pus) : 0); print(" frames/s from here\n");
    sys_sleep(1000);                                          /* the desktop draws the last */
    sys_win_close(id);
    return 0;
}

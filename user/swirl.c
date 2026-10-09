/* swirl.c - the Mandelbrot set in a window of its own (v0.60.153): sauce
 * swirled on a plate.
 *
 *   cook swirl.elf          (from a terminal on the desktop)
 *
 * 320x240, in 16.16 fixed point (ring 3 has no FPU to lean on): each
 * pixel's c is the view's centre plus its offset times the step, and
 * z = z*z + c up to 64 times, products in 64 bits and shifted back by 16
 * (arithmetic shifts, as the tests' Python does). Drawn eight rows at a
 * time, each batch handed to the desktop as it is done, so the window
 * fills as it computes. A click centres the view on that pixel and halves
 * the step (zoom in); - doubles it (zoom out); q or the close box ends it.
 * What it shows is fixed by those integers: scripts/swirl-test.sh redoes
 * the arithmetic on the host and compares every pixel.
 */
#include "ulib.h"

#define SW 320
#define SH 240
#define MAXIT 64
static unsigned px[SW * SH];

static unsigned colour(int it) {                     /* the inside black, the rest a ramp */
    if (it >= MAXIT) return 0;
    unsigned r = (unsigned)(it * 4) & 255, g = (unsigned)(it * 11 + 40) & 255, b = (unsigned)(255 - it * 3) & 255;
    return r << 16 | g << 8 | b;
}
static int escape(int cx, int cy) {                  /* 16.16 */
    long long x = 0, y = 0;
    for (int it = 0; it < MAXIT; it++) {
        long long x2 = (x * x) >> 16, y2 = (y * y) >> 16;
        if (x2 + y2 > (4LL << 16)) return it;
        y = ((2 * x * y) >> 16) + cy;
        x = x2 - y2 + cx;
    }
    return MAXIT;
}
static void render(int id, int cx, int cy, int step) {
    for (int row = 0; row < SH; row++) {
        int y0 = cy + (row - SH / 2) * step;
        for (int col = 0; col < SW; col++)
            px[row * SW + col] = colour(escape(cx + (col - SW / 2) * step, y0));
        if (row % 8 == 7) sys_win_put(id, px);       /* the window fills as it goes */
    }
}

int main(void) {
    int id = sys_win_open(SW, SH, "swirl");
    if (id < 0) { print("swirl: it needs the desktop (run countertop, then swirl in one of its terminals)\n"); return 1; }
    int cx = -(1 << 15), cy = 0, step = (3 << 16) / SW;  /* centre -0.5, 3 wide */
    int views = 1;
    render(id, cx, cy, step);
    for (;;) {
        uwinev_t e;
        int r = sys_win_event(id, &e, 5000);
        if (r < 0) return 0;
        if (r == 0) continue;
        if (e.type == WEV_CLOSE || (e.type == WEV_KEY && e.key == 'q')) break;
        if (e.type == WEV_DOWN && e.x >= 0 && e.y >= 0 && e.x < SW && e.y < SH && step > 1) {
            cx += (e.x - SW / 2) * step; cy += (e.y - SH / 2) * step; step >>= 1;
        } else if (e.type == WEV_KEY && e.key == '-' && step < (1 << 14)) step <<= 1;
        else continue;
        render(id, cx, cy, step);
        views++;
    }
    sys_win_close(id);
    print("swirl: "); print_int(views); print(views == 1 ? " view\n" : " views\n");
    return 0;
}

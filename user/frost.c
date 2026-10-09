/* frost.c - a paint program in a window of its own, for countertop
 * (v0.60.146): icing on the cake.
 *
 *   cook frost.elf          (from a terminal on the desktop)
 *
 * A 400x300 window: a row of colours along the top, the canvas below.
 * Drag on the canvas to draw with the chosen colour (white rubs out),
 * click a colour to choose it, c clears, q or the close box ends it.
 * The first program to use the SYS_WIN_* calls: it draws into its own
 * pixels and hands the desktop the whole body after every batch of
 * events. Serial markers ([frost] ...) let the tests follow it.
 */
#include "ulib.h"

#define FW 400
#define FH 300
#define PAL_H 24
#define NCOL 6
static const unsigned col[NCOL] = { 0x00202020, 0x00C0392B, 0x0027AE60, 0x002E86DE, 0x00E67E22, 0x00FFFFFF };
static unsigned *px;
static int cur;                                      /* the chosen colour */

static void put(int x, int y, unsigned c) { if (x >= 0 && y >= PAL_H && x < FW && y < FH) px[y * FW + x] = c; }
static void rect(int x, int y, int w, int h, unsigned c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) if (i >= 0 && j >= 0 && i < FW && j < FH) px[j * FW + i] = c;
}
static void dab(int x, int y) { for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) put(x + i, y + j, col[cur]); }
static int line(int x0, int y0, int x1, int y1) {     /* Bresenham, a dab at each point; how many */
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy, n = 0;
    for (;;) {
        dab(x0, y0); n++;
        if (x0 == x1 && y0 == y1) return n;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void palette(void) {
    rect(0, 0, FW, PAL_H, 0x00D8DCE2);
    for (int i = 0; i < NCOL; i++) {
        int x = 4 + i * 44;
        if (i == cur) rect(x - 2, 0, 44, PAL_H, 0x00303840);   /* the chosen one, framed */
        rect(x, 2, 40, PAL_H - 4, col[i]);
    }
}
static void clear(void) { rect(0, PAL_H, FW, FH - PAL_H, 0x00FFFFFF); }

static void say(const char *a, int n, const char *b) { eprint("[frost] "); eprint(a); eprint_int(n); eprint(b); eprint("\n"); }

int main(void) {
    int id = sys_win_open(FW, FH, "frost");
    if (id < 0) { print("frost: it needs the desktop (run countertop, then frost in one of its terminals)\n"); eprint("[frost] no window\n"); return 1; }
    px = malloc(FW * FH * 4);
    palette(); clear();
    sys_win_put(id, px);
    say("window ", id, " open");
    int strokes = 0, down = 0, lx = 0, ly = 0, points = 0, sx = 0, sy = 0;
    for (;;) {
        uwinev_t e;
        int r = sys_win_event(id, &e, 2000), changed = 0;
        while (r == 1) {
            if (e.type == WEV_CLOSE || (e.type == WEV_KEY && e.key == 'q')) goto out;
            if (e.type == WEV_KEY && e.key == 'c') { clear(); changed = 1; eprint("[frost] cleared\n"); }
            if (e.type == WEV_DOWN && e.y < PAL_H) {
                int i = (e.x - 4) / 44;
                if (e.x >= 4 && i < NCOL) { cur = i; palette(); changed = 1; say("colour ", cur, ""); }
            } else if (e.type == WEV_DOWN) {
                down = 1; lx = sx = e.x; ly = sy = e.y; points = line(lx, ly, lx, ly); changed = 1;
            } else if (e.type == WEV_MOVE && down) {
                points += line(lx, ly, e.x, e.y); lx = e.x; ly = e.y; changed = 1;
            } else if (e.type == WEV_UP && down) {
                down = 0; strokes++;
                eprint("[frost] stroke "); eprint_int(strokes); eprint(" from "); eprint_int(sx); eprint(","); eprint_int(sy);
                eprint(" to "); eprint_int(lx); eprint(","); eprint_int(ly); eprint(" in colour "); eprint_int(cur); eprint("\n");
                (void)points;
            }
            r = sys_win_event(id, &e, 0);
        }
        if (r < 0) { eprint("[frost] the window went away\n"); return 0; }
        if (changed) sys_win_put(id, px);
    }
out:
    sys_win_close(id);
    print("frost: "); print_int(strokes); print(strokes == 1 ? " stroke\n" : " strokes\n");
    say("closed after ", strokes, " strokes");
    return 0;
}

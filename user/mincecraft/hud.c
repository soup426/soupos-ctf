/* hud.c - the crosshair, the hotbar with counts, the name of the block
 * looked at, and the time (v0.60.195). Text in the kernel's 8x16 font. */
#include "../ulib.h"
#include "hud.h"
#include "world.h"
#include "../../src/font8x16.h"
static void rect(unsigned *b, int x, int y, int w, int h, unsigned c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++)
        if (i >= 0 && j >= 0 && i < BODY_W && j < BODY_H) b[j * BODY_W + i] = c;
}
static void text(unsigned *b, int x, int y, const char *s, unsigned fg) {
    for (; *s; s++, x += 8)
        for (int j = 0; j < 16; j++) for (int i = 0; i < 8; i++)
            if ((font8x16[(unsigned char)*s][j] >> (7 - i)) & 1 && x + i < BODY_W && y + j < BODY_H) b[(y + j) * BODY_W + x + i] = fg;
}
static void num(char *o, int v) { char d[8]; int k = 0, n = 0; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) o[n++] = d[--k]; o[n] = 0; }
void hud_draw(unsigned *b, const player_t *p, const ray_t *tg, int tod) {
    for (int i = -8; i < 8; i++) {                                 /* crosshair: inverted */
        for (int t = 0; t < 2; t++) {
            unsigned *h = &b[(BODY_H / 2 - 1 + t) * BODY_W + BODY_W / 2 + i], *v = &b[(BODY_H / 2 + i) * BODY_W + BODY_W / 2 - 1 + t];
            *h ^= 0xFFFFFF; if (i < -1 || i > 0) *v ^= 0xFFFFFF;
        }
    }
    int ids[9], n = hotbar(p, ids), x0 = BODY_W / 2 - 9 * 44 / 2, y0 = BODY_H - 48;
    for (int s = 0; s < 9; s++) {
        int x = x0 + s * 44;
        rect(b, x, y0, 40, 40, s == p->slot ? 0xFFFFFF : 0x808080);
        rect(b, x + 2, y0 + 2, 36, 36, 0x202020);
        if (s < n) {
            char c[8];
            rect(b, x + 6, y0 + 6, 28, 28, blocks[ids[s]].base);
            num(c, p->inv[ids[s]]);
            text(b, x + 38 - 8 * strlen_(c), y0 + 22, c, 0xFFFFFF);
        }
    }
    if (tg->hit) text(b, 8, 8, blocks[world_get(tg->x, tg->y, tg->z)].name, 0xFFFFFF);
    int m = tod * 1440 / 1200;                                     /* minutes past midnight */
    char clk[6] = { (char)('0' + m / 600), (char)('0' + m / 60 % 10), ':', (char)('0' + m % 60 / 10), (char)('0' + m % 10), 0 };
    text(b, BODY_W - 48, 8, clk, 0xFFFFFF);
}

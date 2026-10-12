/* hud.c - the crosshair, the hotbar with counts, the name of the block
 * looked at, and the time (v0.60.195); the hearts (v0.60.221). Text in the kernel's 8x16 font. */
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
static void text_shadowed(unsigned *b, int x, int y, const char *s, unsigned fg) {   /* readable on any swatch (v0.60.200) */
    text(b, x + 1, y + 1, s, 0x000000);
    text(b, x, y, s, fg);
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
            text_shadowed(b, x + 37 - 8 * strlen_(c), y0 + 21, c, 0xFFFFFF);
        }
    }
    static const unsigned char heart[6] = { 0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08 };   /* (v0.60.221) 7 wide, drawn 2x */
    for (int h = 0; h < P_HEARTS; h++)
        for (int j = 0; j < 6; j++) for (int i = 0; i < 7; i++) if ((heart[j] >> (6 - i)) & 1) {
            rect(b, x0 + h * 16 + i * 2 + 1, y0 - 16 + j * 2 + 1, 2, 2, 0x000000);
            rect(b, x0 + h * 16 + i * 2, y0 - 16 + j * 2, 2, 2, h < p->hp ? 0xE8364Au : 0x3A3A3Au);
        }
    if (tg->hit) text_shadowed(b, 8, 8, blocks[world_get(tg->x, tg->y, tg->z)].name, 0xFFFFFF);
    int m = tod * 1440 / 1200;                                     /* minutes past midnight */
    char clk[6] = { (char)('0' + m / 600), (char)('0' + m / 60 % 10), ':', (char)('0' + m % 60 / 10), (char)('0' + m % 10), 0 };
    text_shadowed(b, BODY_W - 48, 8, clk, 0xFFFFFF);
}
/* F3 (v0.60.242): where the cook is, to a tenth; the way it faces; the
 * frame rate; the block aimed at and its place; the time. */
static int put_num(char *o, int v) { char d[12]; int k = 0, n = 0; if (v < 0) { o[n++] = '-'; v = -v; } do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) o[n++] = d[--k]; return n; }
static int put_str(char *o, const char *s) { int n = 0; while (s[n]) { o[n] = s[n]; n++; } return n; }
static int put_tenth(char *o, fx v) { int n = put_num(o, v >> 16); o[n++] = '.'; o[n++] = (char)('0' + (((v & 0xFFFF) * 10) >> 16)); return n; }
void hud_debug(unsigned *b, const player_t *p, const ray_t *tg, int fps, int tod) {
    char l[5][64]; int n;
    n = put_str(l[0], "Mincecraft  "); n += put_num(l[0] + n, fps); n += put_str(l[0] + n, " fps"); l[0][n] = 0;
    n = put_str(l[1], "XYZ: "); n += put_tenth(l[1] + n, p->x); n += put_str(l[1] + n, " / "); n += put_tenth(l[1] + n, p->y);
    n += put_str(l[1] + n, " / "); n += put_tenth(l[1] + n, p->z); l[1][n] = 0;
    static const char *const dir[4] = { "south (+z)", "east (+x)", "north (-z)", "west (-x)" };   /* yaw 0 faces +z */
    n = put_str(l[2], "Facing: "); n += put_str(l[2] + n, dir[((p->yaw + 128) >> 8) & 3]); n += put_str(l[2] + n, "  yaw "); n += put_num(l[2] + n, p->yaw); l[2][n] = 0;
    n = put_str(l[3], "Block: ");
    if (tg->hit) {
        n += put_str(l[3] + n, blocks[world_get(tg->x, tg->y, tg->z)].name); n += put_str(l[3] + n, " at ");
        n += put_num(l[3] + n, tg->x); l[3][n++] = ' '; n += put_num(l[3] + n, tg->y); l[3][n++] = ' '; n += put_num(l[3] + n, tg->z);
    } else n += put_str(l[3] + n, "none");
    l[3][n] = 0;
    int m = tod * 1440 / 1200;
    n = put_str(l[4], "Time: ");
    l[4][n++] = (char)('0' + m / 600); l[4][n++] = (char)('0' + m / 60 % 10); l[4][n++] = ':'; l[4][n++] = (char)('0' + m % 60 / 10); l[4][n++] = (char)('0' + m % 10); l[4][n] = 0;
    for (int i = 0; i < 5; i++) text_shadowed(b, 8, 28 + 18 * i, l[i], 0xFFFFFF);
}

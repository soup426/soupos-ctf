/* screen.c - neosim's window (v0.60.204): 80 x 30 cells of the kernel's
 * 8x16 font in 640x480. The text on a dark page with its line numbers,
 * a light status line (the mode, the file, [+] when changed, line:column)
 * and the command line. The cursor is an underline in the font's two
 * empty bottom rows, so it never hides a character. */
#include "../ulib.h"
#include "ed.h"
#include "highlight.h"
#include "../../src/font8x16.h"

#define PAGE   0x1A1B26u
#define INK    0xC0CAF5u
#define NUMBER 0x8C96BEu
#define CURNUM 0xE0AF68u
#define BAR    0xC0CAF5u
#define BARINK 0x1A1B26u
#define CURSOR 0xFF9E64u
#define BODY_W 640
#define SELECT 0x33467Cu               /* visual mode's selection (v0.60.206) */
/* The syntax colours (v0.60.217), tokyonight's: plain, keyword, string, number, comment, # line */
static const unsigned hl_colour[] = { INK, 0xBB9AF7u, 0x9ECE6Au, 0xFF9E64u, 0x565F89u, 0x7DCFFFu };
static int selected(const ed_t *e, int y, int x) {
    if (e->mode != M_VISUAL && e->mode != M_VLINE) return 0;
    int ay = e->vy, ax = e->vx, by = e->cy, bx = e->cx;
    if (by < ay || (by == ay && bx < ax)) { int t = ay; ay = by; by = t; t = ax; ax = bx; bx = t; }
    if (y < ay || y > by) return 0;
    if (e->mode == M_VLINE) return 1;
    if (y == ay && x < ax) return 0;
    if (y == by && x > bx) return 0;
    return 1;
}

static void cell(unsigned *b, int c, int r, char ch, unsigned fg, unsigned bg) {
    unsigned *p = b + (r * 16) * BODY_W + c * 8;
    const unsigned char *g = font8x16[(unsigned char)ch];
    for (int j = 0; j < 16; j++, p += BODY_W)
        for (int i = 0; i < 8; i++) p[i] = (g[j] >> (7 - i)) & 1 ? fg : bg;
}
static void put(unsigned *b, int c, int r, const char *s, unsigned fg, unsigned bg) {
    for (; *s && c < SCR_COLS; s++, c++) cell(b, c, r, *s, fg, bg);
}
static int num(char *o, int v) {               /* decimal, no padding; its length */
    char d[12]; int k = 0, n = 0;
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) o[n++] = d[--k];
    o[n] = 0;
    return n;
}
void screen_draw(const ed_t *e, unsigned *b) {
    for (int r = 0; r < SCR_ROWS; r++) for (int c = 0; c < SCR_COLS; c++) cell(b, c, r, ' ', INK, r == TEXT_ROWS ? BAR : PAGE);
    int kind = hl_kind(e->path), state = 0;
    static unsigned char cls[T_COLS + 1];
    for (int l = 0; l < e->top && l < e->t.n; l++) hl_line(kind, e->t.s[l], e->t.len[l], &state, cls);   /* the state at the top */
    for (int r = 0; r < TEXT_ROWS; r++) {
        int l = e->top + r;
        if (l < e->t.n) hl_line(kind, e->t.s[l], e->t.len[l], &state, cls);
        if (l >= e->t.n) { cell(b, 0, r, '~', NUMBER, PAGE); continue; }   /* past the end, as vim */
        char n[12]; int k = num(n, l + 1);
        put(b, NUM_W - 1 - k, r, n, l == e->cy ? CURNUM : NUMBER, PAGE);
        for (int c = 0; c < SCR_COLS - NUM_W; c++) {
            int x = e->left + c;
            if (x >= e->t.len[l]) break;
            cell(b, NUM_W + c, r, e->t.s[l][x], hl_colour[cls[x]], selected(e, l, x) ? SELECT : PAGE);
        }
    }
    /* The cursor: rows 14 and 15 of its cell (two for normal, one for insert). */
    int cr = e->cy - e->top, cc = NUM_W + e->cx - e->left;
    if (e->mode != M_COMMAND && cr >= 0 && cr < TEXT_ROWS && cc >= NUM_W && cc < SCR_COLS)
        for (int j = e->mode == M_INSERT ? 15 : 14; j < 16; j++) for (int i = 0; i < 8; i++) b[(cr * 16 + j) * BODY_W + cc * 8 + i] = CURSOR;
    /* The status line. */
    char st[SCR_COLS + 1]; int n = 0;
    const char *mode = e->mode == M_INSERT ? "-- INSERT --" : e->mode == M_COMMAND ? "COMMAND" :
                       e->mode == M_VISUAL ? "-- VISUAL --" : e->mode == M_VLINE ? "-- VISUAL LINE --" : "NORMAL";
    st[n++] = ' ';
    for (const char *s = mode; *s; s++) st[n++] = *s;
    st[n++] = ' '; st[n++] = ' ';
    for (const char *s = e->path[0] ? e->path : "[No Name]"; *s && n < 60; s++) st[n++] = *s;
    if (e->changed) { st[n++] = ' '; st[n++] = '['; st[n++] = '+'; st[n++] = ']'; }
    st[n] = 0;
    put(b, 0, TEXT_ROWS, st, BARINK, BAR);
    char pos[24]; int p = num(pos, e->cy + 1);
    pos[p++] = ':'; p += num(pos + p, e->cx + 1);
    put(b, SCR_COLS - 1 - p, TEXT_ROWS, pos, BARINK, BAR);
    /* The command line: what is being typed after :, or the last message. */
    if (e->mode == M_COMMAND) {
        cell(b, 0, TEXT_ROWS + 1, e->cmdch ? e->cmdch : ':', INK, PAGE);
        for (int i = 0; i < e->cmdn && i + 1 < SCR_COLS; i++) cell(b, i + 1, TEXT_ROWS + 1, e->cmd[i], INK, PAGE);
        for (int i = 0; i < 8; i++) b[((TEXT_ROWS + 1) * 16 + 15) * BODY_W + (e->cmdn + 1) * 8 + i] = CURSOR;
    } else put(b, 0, TEXT_ROWS + 1, e->msg, INK, PAGE);
}

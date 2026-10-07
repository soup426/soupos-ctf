/* fbcon.c - the text console, drawn onto the linear framebuffer.
 *
 * The design is one sentence: the 80x25 cell buffer at 0xB8000 stays the
 * authoritative console, and a task repaints changed cells as glyphs thirty
 * times a second. Nothing above vga.c changes - not vga.c itself, not the
 * editor's direct cell writes, not the shell - because they all keep writing
 * text cells exactly where they always have. This renderer just watches.
 *
 * That only works because the legacy VGA window at 0xB8000 stays a separate,
 * writable 4 KB of memory while the card scans out the linear framebuffer.
 * Verified on QEMU's std VGA by writing a screenful of text after drawing a
 * pixel pattern and checking the pattern's pixels were untouched. Real
 * hardware is allowed to alias the legacy window into VRAM, in which case
 * text writes would scribble on the picture; that is a documented limitation
 * of the shadow design, not an accident.
 *
 * The diff is 2000 uint16 compares per frame, which is nothing. A repaint is
 * only as expensive as the number of cells that changed; the one full paint
 * happens at start.
 */
#include "fbcon.h"
#include "fb.h"
#include "vga.h"
#include "task.h"
#include "klog.h"
#include "font8x16.h"

#define COLS 80
#define ROWS 25
#define GW   8
#define GH   16

static uint16_t shadow[COLS * ROWS];
static uint32_t ox, oy;              /* centring offsets in pixels */
static int      last_cr = -1, last_cc = -1;
static volatile int paused;      /* a mode 13h program owns the screen */

/* The classic VGA 16-colour palette, as RGB. Attributes index it exactly as
 * the text hardware would: low nibble foreground, next three bits background. */
static const uint32_t pal[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static void draw_cell(int r, int c, uint16_t cell, int cursor) {
    uint8_t ch = (uint8_t)(cell & 0xFF);
    uint8_t at = (uint8_t)(cell >> 8);
    uint32_t fg = pal[at & 0x0F];
    uint32_t bg = pal[(at >> 4) & 0x07];
    const uint8_t *g = font8x16[ch];
    for (int y = 0; y < GH; y++) {
        uint8_t bits = g[y];
        if (cursor && y >= GH - 2) bits = 0xFF;      /* underline cursor */
        for (int x = 0; x < GW; x++)
            fb_put(ox + (uint32_t)(c * GW + x), oy + (uint32_t)(r * GH + y),
                   (bits & (0x80u >> x)) ? fg : bg);
    }
}

static void fbcon_task(void *arg) {
    (void)arg;
    task_set_service();
    volatile uint16_t *cells = (volatile uint16_t *)vga_cells();

    for (;;) {
        if (paused) { task_sleep(50); continue; }

        for (int i = 0; i < COLS * ROWS; i++) {
            uint16_t v = cells[i];
            if (v != shadow[i]) {
                shadow[i] = v;
                draw_cell(i / COLS, i % COLS, v, 0);
            }
        }
        /* The cursor: un-draw the old cell, overdraw the new one. Redrawing
         * one glyph per frame is the whole cost. */
        int cr = vga_get_row(), cc = vga_get_col();
        if ((cr != last_cr || cc != last_cc) &&
            last_cr >= 0 && last_cr < ROWS && last_cc >= 0 && last_cc < COLS)
            draw_cell(last_cr, last_cc, shadow[last_cr * COLS + last_cc], 0);
        last_cr = cr; last_cc = cc;
        if (cr >= 0 && cr < ROWS && cc >= 0 && cc < COLS)
            draw_cell(cr, cc, shadow[cr * COLS + cc], 1);

        task_sleep(33);
    }
}

int fbcon_start(void) {
    if (!fb_present()) return -1;

    ox = (fb_width()  - COLS * GW) / 2;
    oy = (fb_height() - ROWS * GH) / 2;

    fb_clear(0x00101828);                 /* calm border around the console */
    for (int i = 0; i < COLS * ROWS; i++)
        shadow[i] = 0xFFFF;               /* != any real cell: paint everything */

    if (!task_spawn("fbcon", fbcon_task, 0)) return -1;
    klog("[fbcon] 80x25 console at %u,%u on the %ux%u framebuffer\n",
         ox, oy, fb_width(), fb_height());
    return 0;
}

/* Mode 13h programs draw to the whole framebuffer, so the console has to stop
 * painting over them. On resume every cell is marked dirty, because the
 * screen underneath is now a Doom frame rather than the text we last drew. */
void fbcon_pause(void) { paused = 1; }

void fbcon_resume(void) {
    for (int i = 0; i < COLS * ROWS; i++) shadow[i] = 0xFFFF;
    last_cr = last_cc = -1;
    paused = 0;
}

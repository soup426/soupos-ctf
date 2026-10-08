#pragma once
#include <stdint.h>
#include <stddef.h>

/* VGA text mode colors */
typedef enum {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW        = 14,
    VGA_WHITE         = 15,
} vga_color_t;

void vga_init(void);
void vga_clear(void);
void vga_set_color(vga_color_t fg, vga_color_t bg);
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_printf(const char *fmt, ...);
void vga_set_cursor(int row, int col);
int  vga_get_row(void);
int  vga_get_col(void);

/* Fill one row with a character using the current color. Does not move
   the hardware cursor or trigger scroll. Out-of-range rows are ignored. */
void vga_fill_row(int row, char c);

/* Checks that scroll() moves exactly 24 rows and blanks the last one. Saves
 * and restores the real screen; returns 1 on pass. Used by `sample`. */
int vga_scroll_selftest(void);

/* ── Console geometry ───────────────────────────────────────────────────────
 *
 * 80x25 is the hardware text mode and stays the default. A framebuffer boot
 * sets something larger, so NOTHING may assume 80 or 25: ask. The MAX values
 * exist only for sizing static buffers, which cannot be sized at runtime.
 *
 * vga_set_geometry is for the framebuffer console and nothing else; changing
 * it on a text boot would write past the hardware buffer. */
#define VGA_COLS_MAX 128
#define VGA_ROWS_MAX 48

int  vga_cols(void);
int  vga_rows(void);
void vga_set_geometry(int cols, int rows);

/* Like vga_puts, but NOT mirrored to the serial console. For bytes that are
 * glyph codes rather than text - the boot logo's CP437 blocks - where a
 * terminal would show noise. Send the log its own encoding separately. */
void vga_puts_screen_only(const char *s);

/* Turn the serial mirror off for a stretch of ordinary vga_printf output: a
 * screen redrawn in place every second would otherwise pour whole frames into
 * the log and down every remote session. Always turn it back on. */
void vga_set_mirror(int on);

/* The cell store: the hardware text buffer on an ordinary boot, a RAM array
 * on a framebuffer boot (where the legacy window is dead). Anything that
 * pokes cells directly must go through this rather than 0xB8000. */
uint16_t *vga_cells(void);
void      vga_set_backing(uint16_t *cells);

#pragma once
#include <stdint.h>

/* VGA Mode 13h - 320×200 pixels, 256 palette colours.
 * Framebuffer lives at physical 0xA0000 (linear, one byte per pixel).
 * Palette entries are 6-bit (0–63) RGB values written to the DAC. */

#define VGA13_W  320
#define VGA13_H  200

void vga13h_enter(void);   /* switch to mode 13h          */
void vga13h_exit(void);    /* return to 80×25 text mode   */

/* DAC palette - each component is 0–63 */
void vga13h_setpal(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
void vga13h_defpal(void);   /* load a full rainbow palette  */

/* The pixel store: the real 0xA0000 on a text-mode boot, a RAM shadow on a
 * framebuffer boot (where mode 13h does not exist). Never hard-code 0xA0000. */
uint8_t *vga13h_pixels(void);
static inline uint8_t *vga13h_fb(void) { return vga13h_pixels(); }

/* Point the store at the right place; call once, after fb_init. */
void vga13h_init_target(void);

/* Put the store on screen. A no-op on a text boot, where writes are already
 * in video memory; on a framebuffer boot it scales 320x200 up by 3 through
 * the palette. Call it where a mode 13h program would have finished a frame. */
void vga13h_present(void);

/* Single-pixel ops */
static inline void vga13h_plot(int x, int y, uint8_t c) {
    if ((unsigned)x < VGA13_W && (unsigned)y < VGA13_H)
        vga13h_fb()[(unsigned)y * VGA13_W + (unsigned)x] = c;
}
static inline uint8_t vga13h_peek(int x, int y) {
    return vga13h_fb()[(unsigned)y * VGA13_W + (unsigned)x];
}

/* Drawing primitives */
void vga13h_clear(uint8_t c);
void vga13h_fill_rect(int x, int y, int w, int h, uint8_t c);
void vga13h_rect(int x, int y, int w, int h, uint8_t c);
void vga13h_line(int x0, int y0, int x1, int y1, uint8_t c);
void vga13h_circle(int cx, int cy, int r, uint8_t c);
void vga13h_fill_circle(int cx, int cy, int r, uint8_t c);

/* Blit a w×h pixel array at (x,y) */
void vga13h_blit(int x, int y, int w, int h, const uint8_t *pixels);

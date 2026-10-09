#pragma once
#include <stdint.h>

/* The linear framebuffer GRUB hands us when the "soupOS (framebuffer)" menu
 * entry is chosen. 32-bit RGB only; anything else is declined and soupOS stays
 * on the VGA text path.
 *
 * Pitch is bytes per scanline and is NOT width * 4: a row starts at
 * y * pitch. */

/* Call before ANY console output: switches the text cell store to RAM when
 * this boot has a pixel framebuffer (the 0xB8000 window is dead there). */
void     fb_early(uint32_t mb_info_addr);

int      fb_init(uint32_t mb_info_addr);
int      fb_present(void);
uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_pitch_bytes(void);
uint8_t  fb_bits(void);

void     fb_put (uint32_t x, uint32_t y, uint32_t rgb);
void     fb_fill(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t rgb);
void     fb_clear(uint32_t rgb);

/* A run of converted pixels into one scanline, for the mode 13h scaler. */
void     fb_blit_row(uint32_t x, uint32_t y, const uint32_t *px, uint32_t n);

/* vga13h.c - VGA mode 13h driver for soupOS
 *
 * Switching modes without BIOS:  program the VGA register set
 * directly (Miscellaneous Output, Sequencer, CRTC, Graphics
 * Controller, Attribute Controller, DAC) using the well-known
 * mode-13h values from the FreeVGA Hardware Reference.
 *
 * Text-mode restore: all registers are saved before entering
 * mode 13h and fully restored on exit.  The VGA text buffer at
 * 0xB8000 lives in a separate plane that mode 13h never touches,
 * so screen content survives the round-trip.
 */

#include "vga13h.h"
#include "str.h"     /* memset */

/* ── port helpers ─────────────────────────────────────────────── */
static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0,%1"::"a"(v),"Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v;
    __asm__ volatile ("inb %1,%0":"=a"(v):"Nd"(p));
    return v;
}

/* ── saved register state ─────────────────────────────────────── */
static uint8_t sv_misc;
static uint8_t sv_seq[5];
static uint8_t sv_crtc[25];
static uint8_t sv_gc[9];
static uint8_t sv_dac[768];  /* 256 × (R,G,B) - 6-bit each */

/* Text-mode font backup. The character generator bitmap lives in plane 2 of
 * video memory, which mode 13h overwrites with pixel data. Saving and
 * restoring the registers is not enough: without this the glyphs themselves
 * are gone, so every text screen after a graphics command renders as garbage
 * (the symptom was `jot` and `cat` drawing in a tiny broken font). */
static uint8_t sv_font[8192];   /* 256 glyphs x 32 bytes */

/* Map plane 2 linearly at 0xA0000 so the font can be copied in or out. */
static void font_access_begin(void) {
    /* Sequencer: address plane 2 only, sequential (non-chain4) addressing */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);
    outb(0x3C4, 0x04); outb(0x3C5, 0x07);
    /* Graphics controller: read plane 2, odd/even off, 64K window at 0xA0000 */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);
    outb(0x3CE, 0x06); outb(0x3CF, 0x04);
}

static void save_font(void) {
    font_access_begin();
    volatile uint8_t *fb = (volatile uint8_t *)0xA0000;
    for (int i = 0; i < 8192; i++) sv_font[i] = fb[i];
}

static void restore_font(void) {
    font_access_begin();
    volatile uint8_t *fb = (volatile uint8_t *)0xA0000;
    for (int i = 0; i < 8192; i++) fb[i] = sv_font[i];
}

static void save_regs(void) {
    sv_misc = inb(0x3CC);   /* Misc Output readable at 0x3CC */
    for (int i = 0; i < 5;  i++) { outb(0x3C4,i); sv_seq[i]  = inb(0x3C5); }
    for (int i = 0; i < 25; i++) { outb(0x3D4,i); sv_crtc[i] = inb(0x3D5); }
    for (int i = 0; i < 9;  i++) { outb(0x3CE,i); sv_gc[i]   = inb(0x3CF); }
    outb(0x3C7, 0);   /* DAC read address = 0 */
    for (int i = 0; i < 768; i++) sv_dac[i] = inb(0x3C9);
}

static void restore_regs(void) {
    outb(0x3C2, sv_misc);

    /* Sequencer - hold in reset while reprogramming */
    outb(0x3C4, 0x00); outb(0x3C5, 0x01);  /* synchronous reset */
    for (int i = 1; i < 5; i++) { outb(0x3C4,i); outb(0x3C5, sv_seq[i]); }
    outb(0x3C4, 0x00); outb(0x3C5, sv_seq[0]);  /* release */

    /* Unlock CRTC write-protect (CR11 bit 7), then restore all */
    outb(0x3D4, 0x11); outb(0x3D5, inb(0x3D5) & ~0x80u);
    for (int i = 0; i < 25; i++) { outb(0x3D4,i); outb(0x3D5, sv_crtc[i]); }

    /* Graphics controller */
    for (int i = 0; i < 9; i++) { outb(0x3CE,i); outb(0x3CF, sv_gc[i]); }

    /* Attribute controller - use known text-mode values (reading AC is tricky) */
    inb(0x3DA);  /* reset address/data flip-flop */
    for (int i = 0; i < 16; i++) { outb(0x3C0,i); outb(0x3C0,i); } /* identity palette */
    outb(0x3C0, 0x10); outb(0x3C0, 0x0C);  /* Mode Control: text + blink */
    outb(0x3C0, 0x11); outb(0x3C0, 0x00);  /* Overscan: black */
    outb(0x3C0, 0x12); outb(0x3C0, 0x0F);  /* Color Plane Enable: all */
    outb(0x3C0, 0x13); outb(0x3C0, 0x08);  /* Horizontal Panning */
    outb(0x3C0, 0x14); outb(0x3C0, 0x00);  /* Color Select */
    outb(0x3C0, 0x20);  /* re-enable video output */

    /* Restore DAC palette */
    outb(0x3C8, 0);
    for (int i = 0; i < 768; i++) outb(0x3C9, sv_dac[i]);
}

/* ── mode 13h register tables ─────────────────────────────────── */
static const uint8_t m13_seq[5]  = { 0x03, 0x01, 0x0F, 0x00, 0x0E };
static const uint8_t m13_crtc[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,   /* 00–07 */
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* 08–0F */
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,   /* 10–17 */
    0xFF                                                /* 18    */
};
static const uint8_t m13_gc[9]   = { 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF };

/* ── public: enter / exit ─────────────────────────────────────── */
void vga13h_enter(void) {
    save_regs();
    save_font();      /* plane 2 is about to become pixel data */

    outb(0x3C2, 0x63);  /* Miscellaneous Output */

    /* Sequencer */
    outb(0x3C4, 0x00); outb(0x3C5, 0x01);  /* reset */
    for (int i = 1; i < 5; i++) { outb(0x3C4,i); outb(0x3C5, m13_seq[i]); }
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);  /* release */

    /* Unlock CRTC */
    outb(0x3D4, 0x11); outb(0x3D5, inb(0x3D5) & ~0x80u);
    for (int i = 0; i < 25; i++) { outb(0x3D4,i); outb(0x3D5, m13_crtc[i]); }

    /* Graphics Controller */
    for (int i = 0; i < 9; i++) { outb(0x3CE,i); outb(0x3CF, m13_gc[i]); }

    /* Attribute Controller */
    inb(0x3DA);
    for (int i = 0; i < 16; i++) { outb(0x3C0,i); outb(0x3C0,i); }
    outb(0x3C0, 0x10); outb(0x3C0, 0x41);  /* Mode: 256-colour graphics */
    outb(0x3C0, 0x11); outb(0x3C0, 0x00);
    outb(0x3C0, 0x12); outb(0x3C0, 0x0F);
    outb(0x3C0, 0x13); outb(0x3C0, 0x00);
    outb(0x3C0, 0x14); outb(0x3C0, 0x00);
    outb(0x3C0, 0x20);

    /* Entering a graphics mode should give a defined screen. Without this the
     * framebuffer keeps whatever the last mode-13h program left in it, so
     * `bounce` drew its balls over the remains of `vgademo`. */
    {
        volatile uint8_t *fb = (volatile uint8_t *)0xA0000;
        for (uint32_t i = 0; i < (uint32_t)VGA13_W * VGA13_H; i++) fb[i] = 0;
    }
}

void vga13h_exit(void) {
    restore_regs();
    restore_font();
    /* font_access_begin reprogrammed the sequencer and graphics controller to
     * reach plane 2; put the text-mode values back. */
    restore_regs();
}

/* ── palette ──────────────────────────────────────────────────── */
void vga13h_setpal(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    outb(0x3C8, idx);
    outb(0x3C9, r & 0x3F);
    outb(0x3C9, g & 0x3F);
    outb(0x3C9, b & 0x3F);
}

void vga13h_defpal(void) {
    /* Index 0: black (background / transparent by convention) */
    vga13h_setpal(0, 0, 0, 0);

    /* Indices 1–255: full hue wheel (HSV s=1 v=1) */
    for (int i = 1; i < 256; i++) {
        int h6   = i * 6;          /* 6 hue sectors across 255 steps */
        int sec  = h6 / 255;       /* 0..5                            */
        int frac = h6 % 255;       /* 0..254                          */
        int up   = frac * 63 / 254;
        int dn   = (254 - frac) * 63 / 254;
        int r, g, b;
        switch (sec) {
            case 0: r=63; g=up; b=0;  break;
            case 1: r=dn; g=63; b=0;  break;
            case 2: r=0;  g=63; b=up; break;
            case 3: r=0;  g=dn; b=63; break;
            case 4: r=up; g=0;  b=63; break;
            default:r=63; g=0;  b=dn; break;
        }
        vga13h_setpal((uint8_t)i, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
}

/* ── drawing primitives ───────────────────────────────────────── */
void vga13h_clear(uint8_t c) {
    memset(vga13h_fb(), c, (uint32_t)(VGA13_W * VGA13_H));
}

void vga13h_fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int row = y; row < y+h; row++)
        for (int col = x; col < x+w; col++)
            vga13h_plot(col, row, c);
}

void vga13h_rect(int x, int y, int w, int h, uint8_t c) {
    for (int col = x; col < x+w; col++) {
        vga13h_plot(col, y,     c);
        vga13h_plot(col, y+h-1, c);
    }
    for (int row = y; row < y+h; row++) {
        vga13h_plot(x,     row, c);
        vga13h_plot(x+w-1, row, c);
    }
}

void vga13h_line(int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = x1-x0; if (dx<0) dx=-dx;
    int dy = y1-y0; if (dy<0) dy=-dy;
    int sx = x0<x1 ? 1 : -1;
    int sy = y0<y1 ? 1 : -1;
    int err = dx-dy;
    for (;;) {
        vga13h_plot(x0,y0,c);
        if (x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* Midpoint (Bresenham) circle - 8-way symmetry */
void vga13h_circle(int cx, int cy, int r, uint8_t c) {
    int x=0, y=r, d=1-r;
    while (x <= y) {
        vga13h_plot(cx+x,cy+y,c); vga13h_plot(cx-x,cy+y,c);
        vga13h_plot(cx+x,cy-y,c); vga13h_plot(cx-x,cy-y,c);
        vga13h_plot(cx+y,cy+x,c); vga13h_plot(cx-y,cy+x,c);
        vga13h_plot(cx+y,cy-x,c); vga13h_plot(cx-y,cy-x,c);
        if (d < 0) d += 2*x+3;
        else { d += 2*(x-y)+5; y--; }
        x++;
    }
}

/* Filled circle - scanline fill using integer sqrt */
void vga13h_fill_circle(int cx, int cy, int r, uint8_t c) {
    for (int dy = -r; dy <= r; dy++) {
        int r2 = r*r - dy*dy;
        int dx = 0;
        while ((dx+1)*(dx+1) <= r2) dx++;
        for (int col = cx-dx; col <= cx+dx; col++)
            vga13h_plot(col, cy+dy, c);
    }
}

void vga13h_blit(int x, int y, int w, int h, const uint8_t *pixels) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            vga13h_plot(x+col, y+row, pixels[row*w+col]);
}

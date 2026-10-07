/* fb.c - the linear framebuffer GRUB can hand us at boot.
 *
 * soupOS's two existing display paths imitate hardware as old as the hardware
 * they run on: 80x25 text at 0xB8000, and 320x200 mode 13h for Doom. Both are
 * set up by the BIOS before we ever run. This is the third: ask for a linear
 * framebuffer in the multiboot header, and read where it landed out of the
 * info the bootloader already passes to kernel_main.
 *
 * WHICH MODE WE GET IS GRUB'S DECISION, not ours. The multiboot header states
 * a preference; grub.cfg's `gfxpayload` settles it. The default menu entry
 * says `text`, so the text console keeps working and the smoke gate and Doom
 * are untouched; a second entry asks for 1024x768x32 and lands here. That is
 * what "selectable" means in practice, because leaving a graphics mode for a
 * text one needs real-mode BIOS calls that protected mode cannot make.
 *
 * Two things about the geometry:
 *
 *   - PITCH IS NOT WIDTH * BYTES. The bootloader may pad each scanline, so a
 *     row's start is y * pitch, never y * width * bpp/8. Assuming otherwise
 *     draws a picture that shears progressively down the screen.
 *   - The framebuffer is somewhere high in the physical address space, well
 *     past the identity-mapped kernel, so every page of it has to be mapped
 *     before the first write. This is the first thing in soupOS that needs
 *     that; everything else lives in low memory.
 */
#include "fb.h"
#include "multiboot.h"
#include "paging.h"
#include "vga.h"
#include "klog.h"

static uint16_t text_backing[VGA_COLS_MAX * VGA_ROWS_MAX];  /* cell store on FB boots */

static uint8_t *fb;              /* mapped base            */
static uint32_t fb_pitch, fb_w, fb_h;
static uint8_t  fb_bpp;
static int      fb_ready;

int      fb_present(void) { return fb_ready; }
uint32_t fb_width (void)  { return fb_w; }
uint32_t fb_height(void)  { return fb_h; }
uint32_t fb_pitch_bytes(void) { return fb_pitch; }
uint8_t  fb_bits(void)    { return fb_bpp; }

/* Called FIRST THING in kernel_main, before a single character is printed:
 * if this boot has a pixel framebuffer, the text window at 0xB8000 is dead,
 * and every cell written before the switch would vanish. Needs nothing set
 * up - no paging, no heap - which is the point. */
void fb_early(uint32_t mb_info_addr) {
    multiboot_info_t *mb = (multiboot_info_t *)mb_info_addr;
    if (!(mb->flags & MULTIBOOT_FLAG_FRAMEBUFFER)) return;
    if (mb->framebuffer_type == MULTIBOOT_FB_EGA) return;
    for (int i = 0; i < VGA_COLS_MAX * VGA_ROWS_MAX; i++) text_backing[i] = 0x0720;
    vga_set_backing(text_backing);

    /* And the geometry, HERE, before a single character is printed. The cell
     * store's row stride is the column count, so changing it later would
     * re-interpret text already written at the old stride and scramble the
     * screen. 1024x768 with an 8x16 font is exactly 128x48. */
    uint32_t cols = (uint32_t)mb->framebuffer_width  / 8;
    uint32_t rows = (uint32_t)mb->framebuffer_height / 16;
    if (cols > VGA_COLS_MAX) cols = VGA_COLS_MAX;
    if (rows > VGA_ROWS_MAX) rows = VGA_ROWS_MAX;
    if (cols >= 40 && rows >= 10) vga_set_geometry((int)cols, (int)rows);
}

int fb_init(uint32_t mb_info_addr) {
    multiboot_info_t *mb = (multiboot_info_t *)mb_info_addr;

    if (!(mb->flags & MULTIBOOT_FLAG_FRAMEBUFFER)) {
        klog("[fb] bootloader gave no framebuffer info\n");
        return -1;
    }
    if (mb->framebuffer_type == MULTIBOOT_FB_EGA) {
        /* An ordinary text console. Nothing to do: vga.c already owns it. */
        klog("[fb] text mode (%ux%u), staying on the VGA text path\n",
             mb->framebuffer_width, mb->framebuffer_height);
        return -1;
    }
    if (mb->framebuffer_type != MULTIBOOT_FB_RGB || mb->framebuffer_bpp != 32) {
        klog("[fb] unsupported mode: type %u, %u bpp\n",
             mb->framebuffer_type, mb->framebuffer_bpp);
        return -1;
    }

    /* The high 32 bits of the address are not usable from a 32-bit kernel. */
    if (mb->framebuffer_addr >> 32) {
        klog("[fb] framebuffer above 4 GB, unreachable from 32-bit paging\n");
        return -1;
    }

    uint32_t phys = (uint32_t)mb->framebuffer_addr;
    fb_pitch = mb->framebuffer_pitch;
    fb_w     = mb->framebuffer_width;
    fb_h     = mb->framebuffer_height;
    fb_bpp   = mb->framebuffer_bpp;

    /* Map it where it already is: a 1:1 mapping keeps the arithmetic honest
     * and there is nothing else up there to collide with. */
    uint32_t bytes = fb_pitch * fb_h;
    uint32_t base  = phys & ~0xFFFu;
    uint32_t end   = (phys + bytes + 0xFFF) & ~0xFFFu;
    for (uint32_t p = base; p < end; p += 0x1000)
        paging_map(p, p, 0x3);          /* present, writable, supervisor */

    fb       = (uint8_t *)phys;
    fb_ready = 1;
    klog("[fb] %ux%u at %u bpp, pitch %u, %u KB at %x\n",
         fb_w, fb_h, fb_bpp, fb_pitch, bytes / 1024, phys);
    return 0;
}

void fb_put(uint32_t x, uint32_t y, uint32_t rgb) {
    if (!fb_ready || x >= fb_w || y >= fb_h) return;
    *(uint32_t *)(fb + y * fb_pitch + x * 4) = rgb;
}

void fb_fill(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t rgb) {
    if (!fb_ready) return;
    if (x0 >= fb_w || y0 >= fb_h) return;
    if (x0 + w > fb_w) w = fb_w - x0;
    if (y0 + h > fb_h) h = fb_h - y0;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(fb + (y0 + y) * fb_pitch + x0 * 4);
        for (uint32_t x = 0; x < w; x++) row[x] = rgb;
    }
}

void fb_clear(uint32_t rgb) { fb_fill(0, 0, fb_w, fb_h, rgb); }

/* A run of already-converted pixels straight into one scanline. The scaler in
 * vga13h.c builds a row once and lays it down several times through this, so
 * the palette lookup happens once per SOURCE pixel rather than once per
 * destination pixel, and the writes to video memory stay sequential. */
void fb_blit_row(uint32_t x, uint32_t y, const uint32_t *px, uint32_t n) {
    if (!fb_ready || y >= fb_h || x >= fb_w) return;
    if (x + n > fb_w) n = fb_w - x;
    uint32_t *dst = (uint32_t *)(fb + y * fb_pitch + x * 4);
    for (uint32_t i = 0; i < n; i++) dst[i] = px[i];
}

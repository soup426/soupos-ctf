/* countertop.c - the soupOS desktop (v0.60.141).
 *
 * Step 2 of the desktop (queue 48): the surface. A background, a bar along
 * the top with the name and the time, and the pointer where the mouse is.
 *
 * Memory. Everything is drawn into a back buffer and copied to the
 * framebuffer only where something changed. 1024x768 at four bytes a pixel
 * is 3 MB, which would be most of the kernel heap's 8 MB (Doom's zone lives
 * there). But a row is 1024 * 4 = 4096 bytes, exactly a page, and all of
 * RAM is identity-mapped, so the back buffer is a page from the physical
 * allocator for each row and an array of row pointers: 768 pages out of
 * ~29 850 free, nothing taken from the heap, no contiguous run needed.
 *
 * The pointer is never drawn into the back buffer. When a row goes out to
 * the framebuffer the pointer's pixels are laid over it on the way, so
 * moving it only marks where it was and where it is as dirty, and nothing
 * under it ever has to be saved and put back.
 */
#include "countertop.h"
#include "fb.h"
#include "fbcon.h"
#include "mouse.h"
#include "pmm.h"
#include "rtc.h"
#include "task.h"
#include "klog.h"
#include "font8x16.h"

#define MAX_H     1200
#define BAR_H     24
#define BAR_RGB   0x00202428u
#define TEXT_RGB  0x00F0F0F0u
#define DIM_RGB   0x00A0A8B0u
#define TOP_RGB   0x001E3A5Fu      /* the background, top to bottom */
#define BOT_RGB   0x00D87A3Eu

static uint32_t *row[MAX_H];
static int W, H;
static int dx0, dy0, dx1, dy1, any_dirty;      /* the one dirty rectangle, [x0,x1) [y0,y1) */
static int ptr_x, ptr_y;

static void dirty(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x >= x1 || y >= y1) return;
    if (!any_dirty) { dx0 = x; dy0 = y; dx1 = x1; dy1 = y1; any_dirty = 1; return; }
    if (x < dx0) dx0 = x;
    if (y < dy0) dy0 = y;
    if (x1 > dx1) dx1 = x1;
    if (y1 > dy1) dy1 = y1;
}

static void fill(int x, int y, int w, int h, uint32_t c) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= H) continue;
        for (int i = x; i < x + w; i++) if (i >= 0 && i < W) row[j][i] = c;
    }
    dirty(x, y, w, h);
}

static void text(int x, int y, const char *s, uint32_t fg) {
    int x0 = x;
    for (; *s; s++, x += 8) {
        const uint8_t *g = font8x16[(uint8_t)*s];
        for (int j = 0; j < 16; j++) {
            if (y + j < 0 || y + j >= H) continue;
            for (int i = 0; i < 8; i++)
                if ((g[j] >> (7 - i)) & 1 && x + i >= 0 && x + i < W) row[y + j][x + i] = fg;
        }
    }
    dirty(x0, y, x - x0, 16);
}

static uint32_t blend(uint32_t a, uint32_t b, int num, int den) {
    int r = (int)((a >> 16) & 0xFF) + ((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * num / den;
    int g = (int)((a >> 8) & 0xFF) + ((int)((b >> 8) & 0xFF) - (int)((a >> 8) & 0xFF)) * num / den;
    int bl = (int)(a & 0xFF) + ((int)(b & 0xFF) - (int)(a & 0xFF)) * num / den;
    return (uint32_t)(r << 16 | g << 8 | bl);
}

static void background(void) {
    int span = H - BAR_H - 1 > 0 ? H - BAR_H - 1 : 1;
    for (int y = BAR_H; y < H; y++) {
        uint32_t c = blend(TOP_RGB, BOT_RGB, y - BAR_H, span);
        for (int x = 0; x < W; x++) row[y][x] = c;
    }
    dirty(0, BAR_H, W, H - BAR_H);
}

static int clock_sec = -1;
static void bar_clock(void) {
    rtc_time_t t;
    rtc_read(&t);
    if ((int)t.second == clock_sec) return;
    clock_sec = (int)t.second;
    char s[9] = { (char)('0' + t.hour / 10), (char)('0' + t.hour % 10), ':', (char)('0' + t.minute / 10),
                  (char)('0' + t.minute % 10), ':', (char)('0' + t.second / 10), (char)('0' + t.second % 10), 0 };
    fill(W - 8 * 8 - 12, 0, 8 * 8 + 12, BAR_H, BAR_RGB);
    text(W - 8 * 8 - 8, (BAR_H - 16) / 2, s, TEXT_RGB);
}

static void bar(void) {
    fill(0, 0, W, BAR_H, BAR_RGB);
    text(10, (BAR_H - 16) / 2, "soupOS", TEXT_RGB);
    text(10 + 8 * 7, (BAR_H - 16) / 2, "countertop", DIM_RGB);
    clock_sec = -1;
    bar_clock();
}

/* The pointer: an arrow, X the outline, . the fill, its tip at (0,0). */
static const char *const arrow[] = {
    "X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X", "X.......X",
    "X........X", "X.....XXXXX", "X..X..X", "X.X X..X", "XX  X..X", "X    X..X",
    "     X..X", "      XX",
};
#define ARROW_H (int)(sizeof(arrow) / sizeof(arrow[0]))
#define ARROW_W 11

static void flush(void) {
    if (!any_dirty) return;
    static uint32_t line[2048];
    for (int y = dy0; y < dy1; y++) {
        int n = dx1 - dx0;
        for (int i = 0; i < n; i++) line[i] = row[y][dx0 + i];
        int ay = y - ptr_y;
        if (ay >= 0 && ay < ARROW_H) {
            const char *a = arrow[ay];
            for (int i = 0; a[i]; i++) {
                int x = ptr_x + i;
                if (x < dx0 || x >= dx1) continue;
                if (a[i] == 'X') line[x - dx0] = 0x00000000u;
                else if (a[i] == '.') line[x - dx0] = 0x00FFFFFFu;
            }
        }
        fb_blit_row((uint32_t)dx0, (uint32_t)y, line, (uint32_t)n);
    }
    any_dirty = 0;
}

static void release(int pages) {
    for (int i = 0; i < pages; i++) { pmm_free_page(row[i]); row[i] = 0; }
}

int countertop_run(int (*avail)(void), int (*getch)(void)) {
    if (!fb_present()) return -1;
    W = (int)fb_width(); H = (int)fb_height();
    if (H > MAX_H || W > 2048) return -1;
    for (int y = 0; y < H; y++) {
        row[y] = (uint32_t *)pmm_alloc_page();
        if (!row[y] || (uint32_t)W * 4 > 4096) { release(y); return -2; }
    }
    fbcon_pause();
    any_dirty = 0;
    background();
    bar();
    int b;
    mouse_get_px(&ptr_x, &ptr_y, &b);
    flush();
    klog("[countertop] up %dx%d, %d pages for the back buffer\n", W, H, H);
    for (;;) {
        int px, py;
        mouse_get_px(&px, &py, &b);
        if (px != ptr_x || py != ptr_y) {
            dirty(ptr_x, ptr_y, ARROW_W, ARROW_H);
            ptr_x = px; ptr_y = py;
            dirty(ptr_x, ptr_y, ARROW_W, ARROW_H);
        }
        bar_clock();
        if (avail()) { if (getch() == 27) break; }
        flush();
        task_sleep(15);
    }
    release(H);
    fb_clear(0x00101828);                 /* the console's border, as vga13h leaves it */
    fbcon_resume();
    klog("[countertop] down\n");
    return 0;
}

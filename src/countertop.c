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
 *
 * Windows (v0.60.142, step 3). A list in z-order, each a border, a title
 * bar (its name, a close box) and a body. Nothing keeps a copy of what a
 * window covers: when something changes, the rectangle it touched is
 * repainted from scratch, the background first and then every window from
 * the bottom up, all clipped to that rectangle, so overlap comes out right
 * by construction. A press raises and focuses the window under it (the
 * close box closes it; the title bar starts a drag that follows the
 * pointer until the button comes up).
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

/* What drawing may touch: the whole screen, or while repainting, the
 * rectangle being repainted. [cx0,cx1) [cy0,cy1) */
static int cx0, cy0, cx1, cy1;
static void clip_all(void) { cx0 = 0; cy0 = 0; cx1 = W; cy1 = H; }

static void fill(int x, int y, int w, int h, uint32_t c) {
    int xa = x < cx0 ? cx0 : x, xb = x + w > cx1 ? cx1 : x + w;
    int ya = y < cy0 ? cy0 : y, yb = y + h > cy1 ? cy1 : y + h;
    for (int j = ya; j < yb; j++)
        for (int i = xa; i < xb; i++) row[j][i] = c;
    dirty(x, y, w, h);
}

static void text(int x, int y, const char *s, uint32_t fg) {
    int x0 = x;
    for (; *s; s++, x += 8) {
        const uint8_t *g = font8x16[(uint8_t)*s];
        for (int j = 0; j < 16; j++) {
            if (y + j < cy0 || y + j >= cy1) continue;
            for (int i = 0; i < 8; i++)
                if ((g[j] >> (7 - i)) & 1 && x + i >= cx0 && x + i < cx1) row[y + j][x + i] = fg;
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

static void background(void) {                  /* within the clip */
    int span = H - BAR_H - 1 > 0 ? H - BAR_H - 1 : 1;
    int ya = cy0 > BAR_H ? cy0 : BAR_H;
    for (int y = ya; y < cy1; y++) {
        uint32_t c = blend(TOP_RGB, BOT_RGB, y - BAR_H, span);
        for (int x = cx0; x < cx1; x++) row[y][x] = c;
    }
    dirty(cx0, ya, cx1 - cx0, cy1 - ya);
}

/* ---- windows ---------------------------------------------------------- */

#define MAX_WIN   16
#define TITLE_H   22
#define BORDER    1
#define CLOSE_SZ  16
#define EDGE_RGB  0x00101418u
#define FOCUS_RGB 0x003A6EA5u
#define BLUR_RGB  0x005A5F66u
#define SHUT_RGB  0x00C0504Du
typedef struct {
    int used, x, y, w, h;
    char title[32];
    uint32_t body;
    const char *lines[8];
} win_t;
static win_t win[MAX_WIN];
static int order[MAX_WIN], nwin;                 /* z-order, bottom first */
static int drag = -1, drag_dx, drag_dy, drag_ox, drag_oy, last_btn;

static void scopy(char *d, const char *s, int n) { int i = 0; for (; s[i] && i < n - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static int  topmost(void) { return nwin ? order[nwin - 1] : -1; }

static void draw_window(int k) {
    win_t *w = &win[k];
    int focused = k == topmost();
    fill(w->x, w->y, w->w, w->h, EDGE_RGB);
    fill(w->x + BORDER, w->y + BORDER, w->w - 2 * BORDER, TITLE_H, focused ? FOCUS_RGB : BLUR_RGB);
    text(w->x + BORDER + 8, w->y + BORDER + (TITLE_H - 16) / 2, w->title, TEXT_RGB);
    int bx = w->x + w->w - BORDER - 3 - CLOSE_SZ, by = w->y + BORDER + (TITLE_H - CLOSE_SZ) / 2;
    fill(bx, by, CLOSE_SZ, CLOSE_SZ, SHUT_RGB);
    for (int i = 3; i < CLOSE_SZ - 3; i++) {            /* an X across it */
        fill(bx + i, by + i, 1, 1, TEXT_RGB);
        fill(bx + CLOSE_SZ - 1 - i, by + i, 1, 1, TEXT_RGB);
    }
    int top = w->y + BORDER + TITLE_H;
    fill(w->x + BORDER, top, w->w - 2 * BORDER, w->h - 2 * BORDER - TITLE_H, w->body);
    for (int i = 0; i < 8 && w->lines[i]; i++) text(w->x + BORDER + 10, top + 8 + i * 18, w->lines[i], 0x00202020u);
}

/* Everything in [x,y,w,h) drawn again: the background, then the windows
 * bottom to top, each clipped to it. The bar is left alone. */
static void repaint(int x, int y, int w, int h) {
    cx0 = x < 0 ? 0 : x; cy0 = y < BAR_H ? BAR_H : y;
    cx1 = x + w > W ? W : x + w; cy1 = y + h > H ? H : y + h;
    if (cx0 < cx1 && cy0 < cy1) {
        background();
        for (int i = 0; i < nwin; i++) draw_window(order[i]);
    }
    clip_all();
}
static void repaint_win(int k) { repaint(win[k].x, win[k].y, win[k].w, win[k].h); }

static int open_window(int x, int y, int w, int h, const char *title, uint32_t body, const char *const *lines) {
    for (int k = 0; k < MAX_WIN; k++) if (!win[k].used) {
        win_t *v = &win[k];
        v->used = 1; v->x = x; v->y = y; v->w = w; v->h = h; v->body = body;
        scopy(v->title, title, sizeof(v->title));
        for (int i = 0; i < 8; i++) v->lines[i] = lines && lines[i] ? lines[i] : 0;
        int was = topmost();
        order[nwin++] = k;
        if (was >= 0) repaint_win(was);           /* its title bar goes grey */
        repaint_win(k);
        return k;
    }
    return -1;
}
static void raise_window(int k) {
    int at = -1;
    for (int i = 0; i < nwin; i++) if (order[i] == k) at = i;
    if (at < 0 || at == nwin - 1) return;
    int was = topmost();
    for (int i = at; i < nwin - 1; i++) order[i] = order[i + 1];
    order[nwin - 1] = k;
    repaint_win(was);
    repaint_win(k);
    klog("[countertop] raise %s\n", win[k].title);
}
static void close_window(int k) {
    int at = -1;
    for (int i = 0; i < nwin; i++) if (order[i] == k) at = i;
    if (at < 0) return;
    for (int i = at; i < nwin - 1; i++) order[i] = order[i + 1];
    nwin--;
    win[k].used = 0;
    klog("[countertop] close %s\n", win[k].title);
    repaint_win(k);                               /* what it covered */
    if (topmost() >= 0) repaint_win(topmost());   /* the new focus's title bar */
}
static int window_at(int x, int y) {
    for (int i = nwin - 1; i >= 0; i--) {
        win_t *w = &win[order[i]];
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return order[i];
    }
    return -1;
}

/* The mouse's buttons, once a tick: a press picks a window, a drag moves
 * it, the close box closes it. */
static void pointer_buttons(int px, int py, int btn) {
    int left = btn & 1, was = last_btn & 1;
    last_btn = btn;
    if (left && !was) {
        int k = window_at(px, py);
        if (k < 0) return;
        win_t *w = &win[k];
        int bx = w->x + w->w - BORDER - 3 - CLOSE_SZ, by = w->y + BORDER + (TITLE_H - CLOSE_SZ) / 2;
        if (px >= bx && px < bx + CLOSE_SZ && py >= by && py < by + CLOSE_SZ) { close_window(k); return; }
        raise_window(k);
        if (py < w->y + BORDER + TITLE_H) { drag = k; drag_dx = px - w->x; drag_dy = py - w->y; drag_ox = w->x; drag_oy = w->y; }
        return;
    }
    if (left && drag >= 0) {
        win_t *w = &win[drag];
        int nx = px - drag_dx, ny = py - drag_dy;
        if (ny < BAR_H) ny = BAR_H;                       /* not under the bar */
        if (ny > H - TITLE_H) ny = H - TITLE_H;           /* a title bar left to grab */
        if (nx > W - 40) nx = W - 40;
        if (nx < 40 - w->w) nx = 40 - w->w;
        if (nx != w->x || ny != w->y) {
            int ox = w->x, oy = w->y;
            w->x = nx; w->y = ny;
            repaint(ox, oy, w->w, w->h);
            repaint_win(drag);
        }
        return;
    }
    if (!left && drag >= 0) {
        if (win[drag].x != drag_ox || win[drag].y != drag_oy)   /* a click on the title is not a move */
            klog("[countertop] moved %s to %d,%d\n", win[drag].title, win[drag].x, win[drag].y);
        drag = -1;
    }
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
    clip_all();
    background();
    bar();
    nwin = 0; drag = -1; last_btn = 0;
    for (int k = 0; k < MAX_WIN; k++) win[k].used = 0;
    static const char *const welcome[] = { "Welcome to countertop.", "", "Drag a window by its title bar,",
                                           "click one to bring it forward,", "and the red box closes it.", "",
                                           "Esc gives the console back.", 0 };
    static const char *const pantry[] = { "The pantry.", "", "Windows of their own come next:",
                                          "terminals, files, programs.", 0 };
    open_window(120, 100, 420, 220, "Welcome", 0x00F4F1EAu, welcome);
    open_window(420, 260, 360, 240, "Pantry", 0x00E6EEF5u, pantry);
    int b;
    mouse_get_px(&ptr_x, &ptr_y, &b);
    last_btn = b;
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
        if (b != last_btn || drag >= 0) pointer_buttons(px, py, b);
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

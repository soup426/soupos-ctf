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
 *
 * Terminals (v0.60.143, step 4). A window can hold a wterm_t: a term_t of
 * 80x25 cells (a character and a VGA attribute each, as the console keeps
 * them) with a ring of keys, and a shell session of its own on it, as the
 * SSH server gives each channel: its own task, cook, cwd, history and
 * foreground job. Its output lands in the cells and the window is drawn
 * again; the keyboard goes to the focused window's ring (Ctrl-C and Ctrl-Z
 * to its foreground program, as the console's IRQ does). Closing it hangs
 * the session up (its programs killed, its getc -1); typing clockout or
 * exit in it closes the window. Its memory goes only once its task is done.
 *
 * The taskbar (v0.60.144, step 5). Along the bottom: a Terminal launcher,
 * then a button for every window (blue for the focused one, dim for a
 * minimised one): a click restores a minimised window, minimises the
 * focused one, raises any other. A window's minimise box hides it; hidden
 * windows are not drawn, not hit, and not the focus. Windows live between
 * the two bars.
 *
 * Files (v0.60.145, step 6). A window listing a bowl: its path, then ..,
 * its bowls and its files (with their sizes). A click selects, a double
 * click (or Enter, with the arrows to move) opens: a bowl in the same
 * window, a file in a new terminal with `pour PATH` typed into it (`jot
 * PATH` for a text file, v0.60.154), so what may be read is the shell's to
 * decide, as it always is.
 *
 * A terminal's bottom-right corner drags it to a new size (v0.60.162).
 * The wheel (v0.60.151) scrolls the files window under the pointer, and
 * is a program's event over its window (v0.60.157); Alt+Tab
 * (v0.60.155) sends the focused window to the back.
 *
 * Programs' windows (v0.60.146, step 7). SYS_WIN_OPEN/PUT/EVENT/CLOSE: a
 * ring-3 program asks for a window, hands over its whole body of pixels,
 * and reads its events (keys while focused, a press/drag/release in its
 * body, the close box). Those calls only fill a slot in app[]; this loop
 * draws. A program's window goes when it closes it or ends (killed too).
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
#include "term.h"
#include "net.h"
#include "rtl8139.h"
#include "power.h"
#include "shell.h"
#include "heap.h"
#include "proc.h"
#include "str.h"
#include "fat.h"
#include "users.h"
#include "keyboard.h"
#include "syscall_nr.h"
#include "timer.h"
#include "clip.h"

#define MAX_H     1200
#define BAR_H     24
#define BAR_RGB   0x00202428u
#define TEXT_RGB  0x00F0F0F0u
#define DIM_RGB   0x00A0A8B0u
#define TOP_RGB   0x001E3A5Fu      /* the background, top to bottom */
#define BOT_RGB   0x00D87A3Eu
/* The bar's buttons (v0.60.143): [x, x+w) along it. */
#define BTN_TERM_X 170
#define BTN_TERM_W (8 * 10)
#define BTN_LEAVE_X (BTN_TERM_X + BTN_TERM_W + 10)
#define BTN_LEAVE_W (8 * 7)
#define BTN_RGB    0x003A3F48u
#define TASK_H     28                    /* the taskbar, along the bottom (v0.60.144) */
#define TBTN_W     150
#define BTN_FILES_X (BTN_LEAVE_X + BTN_LEAVE_W + 10)
#define BTN_FILES_W (8 * 7)
#define BTN_KITCHEN_X (BTN_FILES_X + BTN_FILES_W + 10)   /* (v0.60.158) the programs */
#define BTN_KITCHEN_W (8 * 9)
#define LAUNCH_W   (8 * 10 + 16)

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

/* The backgrounds (v0.60.169): a cook chooses one from the Kitchen menu
 * and it is kept in their home (`countertop`, beside `profile`: /countertop
 * for the headchef), read back when the desktop starts. */
static const struct { const char *name; uint32_t top, bot; } bgs[] = {
    { "dusk",   TOP_RGB,     BOT_RGB },
    { "sea",    0x000B2E4Au, 0x003FA7B8u },
    { "forest", 0x00123524u, 0x008DB86Bu },
    { "plum",   0x002A1238u, 0x00C0607Eu },
};
#define NBG (int)(sizeof(bgs) / sizeof(bgs[0]))
static int bg_choice;
/* A picture for the background (v0.60.182): a 24-bit BMP named by a
 * "picture <path>" line in the cook's settings, up to the screen's size,
 * centred between the bars over the gradient. Its rows are pages, as the
 * back buffer's are (1024 pixels of 4 bytes is one page). */
static uint32_t *pic_row[MAX_H];
static int pic_w, pic_h;
static char pic_path[FAT_PATH_MAX];
static void background(void) {                  /* within the clip */
    int span = H - BAR_H - 1 > 0 ? H - BAR_H - 1 : 1;
    int ya = cy0 > BAR_H ? cy0 : BAR_H;
    int px0 = (W - pic_w) / 2, py0 = BAR_H + (H - BAR_H - TASK_H - pic_h) / 2;
    if (py0 < BAR_H) py0 = BAR_H;
    for (int y = ya; y < cy1; y++) {
        uint32_t c = blend(bgs[bg_choice].top, bgs[bg_choice].bot, y - BAR_H, span);
        if (pic_h && y >= py0 && y < py0 + pic_h) {
            const uint32_t *src = pic_row[y - py0] - px0;
            for (int x = cx0; x < cx1; x++) row[y][x] = x >= px0 && x < px0 + pic_w ? src[x] : c;
        } else
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
typedef struct wterm wterm_t;
typedef struct {
    int used, x, y, w, h;
    char title[32];
    uint32_t body;
    const char *lines[8];
    wterm_t *term;                      /* a terminal window's cells, or 0 */
    void *sess;                         /* and the shell session on them */
    int min;                            /* minimised: on the taskbar only */
    struct files *files;                /* a files window's listing, or 0 */
    int app;                            /* a program's window: its app[] slot, or -1 */
    int maxed, rx, ry, rw, rh;          /* (v0.60.161) maximised, and where to put it back */
    int snapped, rcols, rrows;          /* (v0.60.167) 1 left half, 2 right; a terminal's own size */
    int hidden;                         /* (v0.60.178) a Kitchen terminal: never shown */
    struct platter *plat;               /* (v0.60.186) a picture's window */
} win_t;
static win_t win[MAX_WIN];
static int order[MAX_WIN], nwin;                 /* z-order, bottom first */
static int drag = -1, drag_dx, drag_dy, drag_ox, drag_oy, last_btn;
static uint32_t presses_seen;                    /* mouse_presses() when last looked */
static int wheel_seen;                           /* and mouse_wheel() (v0.60.151) */

static void scopy(char *d, const char *s, int n) { int i = 0; for (; s[i] && i < n - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static int  topmost(void) {                      /* the focus: the top window not minimised */
    for (int i = nwin - 1; i >= 0; i--) if (!win[order[i]].min) return order[i];
    return -1;
}

/* ---- terminal windows (v0.60.143) --------------------------------------- */

#define TCOLS 80                        /* a new terminal's size */
#define TROWS 25
#define WMAXC 127                       /* the most that fit between the bars (v0.60.162) */
#define WMAXR 43
#define SB_LINES 300                    /* scrollback, lines a terminal keeps (v0.60.166) */
struct wterm {
    term_t t;                           /* first: a wterm_t is a term_t */
    uint16_t cell[WMAXR * WMAXC];       /* char | attribute << 8, as the console's; cols to a row */
    int cols, rows;                     /* (v0.60.162) its size now */
    int row, col, attr;
    volatile int closed, changed;
    int sel_a, sel_b;                   /* (v0.60.160) a selection, cell indices; sel_a -1: none */
    uint16_t *sb;                       /* (v0.60.166) the lines scrolled off the top: SB_LINES of WMAXC */
    uint8_t sb_w[SB_LINES];             /* each one's width when it went */
    int sb_head, sb_n, back;            /* the next slot, how many, and how far the view is scrolled back */
    int ring[128];
    volatile uint32_t head, tail;
};
static const uint32_t vga_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};
static wterm_t *WT(term_t *t) { return (wterm_t *)t; }
static void wt_scroll(wterm_t *w) {
    if (w->sb) {                                        /* the top row into the scrollback */
        uint16_t *dst = w->sb + w->sb_head * WMAXC;
        memcpy(dst, w->cell, sizeof(uint16_t) * (uint32_t)w->cols);
        w->sb_w[w->sb_head] = (uint8_t)w->cols;
        w->sb_head = (w->sb_head + 1) % SB_LINES;
        if (w->sb_n < SB_LINES) w->sb_n++;
    }
    memmove(w->cell, w->cell + w->cols, sizeof(w->cell[0]) * w->cols * (w->rows - 1));
    for (int c = 0; c < w->cols; c++) w->cell[(w->rows - 1) * w->cols + c] = (uint16_t)(' ' | w->attr << 8);
    w->row = w->rows - 1;
}
static void wt_char(wterm_t *w, char c) {
    if (c == '\n') { w->col = 0; if (++w->row >= w->rows) wt_scroll(w); }
    else if (c == '\r') w->col = 0;
    else if (c == '\b') { if (w->col > 0) { w->col--; w->cell[w->row * w->cols + w->col] = (uint16_t)(' ' | w->attr << 8); } }
    else if (c == '\t') { do wt_char(w, ' '); while (w->col % 8); }
    else if ((unsigned char)c >= ' ') {
        w->cell[w->row * w->cols + w->col] = (uint16_t)((unsigned char)c | w->attr << 8);
        if (++w->col >= w->cols) { w->col = 0; if (++w->row >= w->rows) wt_scroll(w); }
    }
    w->changed = 1;
}
static void wt_puts(term_t *t, const char *z) { while (*z) wt_char(WT(t), *z++); }
static void wt_putc(term_t *t, char c) { wt_char(WT(t), c); }
static void wt_color(term_t *t, int fg, int bg) { WT(t)->attr = (fg & 15) | (bg & 7) << 4; }
static void wt_cursor(term_t *t, int r, int c) {
    wterm_t *w = WT(t);
    w->row = r < 0 ? 0 : r >= w->rows ? w->rows - 1 : r;
    w->col = c < 0 ? 0 : c >= w->cols ? w->cols - 1 : c;
    w->changed = 1;
}
static int  wt_row(term_t *t)  { return WT(t)->row; }
static int  wt_col(term_t *t)  { return WT(t)->col; }
static int  wt_cols(term_t *t) { return WT(t)->cols; }
static int  wt_rows(term_t *t) { return WT(t)->rows; }
static void wt_clear(term_t *t) {
    wterm_t *w = WT(t);
    for (int i = 0; i < w->rows * w->cols; i++) w->cell[i] = (uint16_t)(' ' | w->attr << 8);
    w->row = w->col = 0; w->changed = 1;
}
static int wt_avail(term_t *t) { wterm_t *w = WT(t); return w->closed || w->head != w->tail; }
static int wt_getc(term_t *t) {
    wterm_t *w = WT(t);
    while (w->head == w->tail) {
        if (w->closed) return -1;
        task_sleep(10);
    }
    int k = w->ring[w->tail % 128];
    w->tail++;
    return k;
}
static void wt_flush(term_t *t) { wterm_t *w = WT(t); w->tail = w->head; }
/* A new size (v0.60.162): the rows around the cursor are kept, so the
 * prompt stays in view, and each row is cut or blank-padded to the new
 * width; the selection goes. */
static void wt_resize(wterm_t *w, int nc, int nr) {
    static uint16_t old[WMAXR * WMAXC];
    int oc = w->cols, orows = w->rows;
    memcpy(old, w->cell, sizeof(old[0]) * (uint32_t)(oc * orows));
    int shift = w->row >= nr ? w->row - (nr - 1) : 0;
    for (int r = 0; r < nr; r++)
        for (int c = 0; c < nc; c++)
            w->cell[r * nc + c] = r + shift < orows && c < oc ? old[(r + shift) * oc + c] : (uint16_t)(' ' | w->attr << 8);
    w->cols = nc; w->rows = nr;
    w->row -= shift;
    if (w->col >= nc) w->col = nc - 1;
    w->sel_a = w->sel_b = -1;
    w->back = 0;
    w->changed = 1;
}
static void wt_cell(term_t *t, int r, int c, char ch, int attr) {   /* (v0.60.154) for jot */
    wterm_t *w = WT(t);
    if (r < 0 || r >= w->rows || c < 0 || c >= w->cols) return;
    w->cell[r * w->cols + c] = (uint16_t)((unsigned char)ch | (attr & 0x7F) << 8);
    w->changed = 1;
}
/* A key for the window: Ctrl-C and Ctrl-Z to its foreground program, as
 * the keyboard IRQ does for the console and the stream terminal for SSH. */
static void wt_key(wterm_t *w, int key) {
    if (key == 0x03) w->t.intr++;
    if ((key == 0x03 || key == 0x1A) && w->t.fg) {
        if (key == 0x03) proc_kill(w->t.fg->pid);
        else             proc_flag_stop(w->t.fg);
        return;
    }
    if (w->head - w->tail >= 128) return;
    w->ring[w->head % 128] = key;
    w->head++;
}
static wterm_t *wt_new(void) {
    wterm_t *w = kmalloc(sizeof(wterm_t));
    if (!w) return 0;
    memset(w, 0, sizeof(*w));
    w->t.name = "countertop";
    w->t.puts = wt_puts; w->t.putc = wt_putc; w->t.set_color = wt_color; w->t.set_cursor = wt_cursor;
    w->t.row = wt_row; w->t.col = wt_col; w->t.cols = wt_cols; w->t.rows = wt_rows; w->t.clear = wt_clear;
    w->t.getc = wt_getc; w->t.available = wt_avail; w->t.flush_in = wt_flush; w->t.put_cell = wt_cell;
    w->t.ctx = w;
    w->attr = 0x07;
    w->cols = TCOLS; w->rows = TROWS;
    w->sb = kmalloc(sizeof(uint16_t) * SB_LINES * WMAXC);   /* none if the heap says no: no scrollback */
    w->sb_head = w->sb_n = w->back = 0;
    w->sel_a = w->sel_b = -1;
    wt_clear(&w->t);
    return w;
}
/* The cells into the body at (x,y), within the clip; the cursor when focused. */
/* What row r, column c shows with the view scrolled back w->back lines:
 * the scrollback (oldest first) then the live rows, as one long buffer. */
static uint16_t view_cell(const wterm_t *w, int r, int c) {
    int i = w->sb_n - w->back + r;                     /* index into scrollback + live */
    if (i >= w->sb_n) return w->cell[(i - w->sb_n) * w->cols + c];
    int slot = (w->sb_head - w->sb_n + i + SB_LINES) % SB_LINES;
    return c < w->sb_w[slot] ? w->sb[slot * WMAXC + c] : (uint16_t)(' ' | 0x07 << 8);
}
static void draw_cells(wterm_t *w, int x, int y, int focused) {
    for (int r = 0; r < w->rows; r++) {
        int py = y + r * 16;
        if (py + 16 <= cy0 || py >= cy1) continue;
        for (int c = 0; c < w->cols; c++) {
            int px = x + c * 8;
            if (px + 8 <= cx0 || px >= cx1) continue;
            uint16_t cell = view_cell(w, r, c);
            uint32_t fg = vga_rgb[(cell >> 8) & 15], bg = vga_rgb[(cell >> 12) & 7];
            if (w->sel_a >= 0) {                                /* selected: inverted */
                int i = r * w->cols + c, lo = w->sel_a < w->sel_b ? w->sel_a : w->sel_b, hi = w->sel_a ^ w->sel_b ^ lo;
                if (i >= lo && i <= hi) { uint32_t t = fg; fg = bg; bg = t; }
            }
            const uint8_t *g = font8x16[cell & 0xFF];
            int cur = focused && !w->closed && !w->back && r == w->row && c == w->col;
            for (int j = 0; j < 16; j++) {
                if (py + j < cy0 || py + j >= cy1) continue;
                uint32_t *dst = row[py + j];
                for (int i = 0; i < 8; i++) {
                    if (px + i < cx0 || px + i >= cx1) continue;
                    int on = (g[j] >> (7 - i)) & 1;
                    if (cur && j >= 14) on = 1;                 /* an underline cursor */
                    dst[px + i] = on ? fg : bg;
                }
            }
        }
    }
    dirty(x, y, w->cols * 8, w->rows * 16);
}

/* ---- files windows (v0.60.145) ----------------------------------------- */

#define FMAX   256
#define FROW_H 18
#define DCLICK 60                        /* ticks between a double click's presses */
struct files {
    char path[FAT_PATH_MAX];
    int n, sel, top;
    char name[FMAX][FAT_LFN_MAX];
    uint8_t dir[FMAX];
    uint32_t size[FMAX];
};
static void files_load(struct files *f) {
    f->n = 0; f->sel = 0; f->top = 0;
    if (f->path[1]) { strcpy(f->name[0], ".."); f->dir[0] = 1; f->size[0] = 0; f->n = 1; }
    if (!users_may(f->path, 'r')) return;
    static fat_entry_t e[FAT_LS_MAX];
    for (int pass = 0; pass < 2; pass++)            /* bowls, then files */
        for (int skip = 0, got; (got = fat_ls_from(f->path, e, FAT_LS_MAX, skip)) > 0; skip += got) {
            for (int i = 0; i < got && f->n < FMAX; i++) {
                int d = (e[i].attr & FAT_ATTR_DIR) != 0;
                const char *nm = fat_display_name(&e[i]);
                if (d != !pass || nm[0] == '.') continue;
                strncpy(f->name[f->n], nm, FAT_LFN_MAX - 1); f->name[f->n][FAT_LFN_MAX - 1] = '\0';
                f->dir[f->n] = (uint8_t)d; f->size[f->n] = e[i].size; f->n++;
            }
            if (got < FAT_LS_MAX) break;
        }
}
static void put_size(char *o, uint32_t v) {
    char d[12]; int k = 0;
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    int n = 0; while (k) o[n++] = d[--k];
    o[n] = '\0';
}
static int files_rows(const win_t *w) { return (w->h - 2 * BORDER - TITLE_H - 28) / FROW_H; }
static void draw_files(const win_t *w, int focused) {
    struct files *f = w->files;
    int x = w->x + BORDER, y = w->y + BORDER + TITLE_H, wd = w->w - 2 * BORDER;
    fill(x, y, wd, w->h - 2 * BORDER - TITLE_H, 0x00F7F7F2u);
    fill(x, y, wd, 24, 0x00E2E4E8u);                 /* the path */
    text(x + 8, y + 4, f->path, 0x00202020u);
    int rows = files_rows(w);
    for (int i = 0; i < rows && f->top + i < f->n; i++) {
        int k = f->top + i, ry = y + 28 + i * FROW_H;
        int sel = k == f->sel;
        if (sel) fill(x + 2, ry, wd - 4, FROW_H, focused ? FOCUS_RGB : 0x00C8CCD2u);
        uint32_t fg = sel && focused ? TEXT_RGB : f->dir[k] ? 0x001E4E8Cu : 0x00202020u;
        char line[FAT_LFN_MAX + 2]; int n = 0;
        for (const char *c = f->name[k]; *c && n < FAT_LFN_MAX - 1; c++) line[n++] = *c;
        if (f->dir[k] && strcmp(f->name[k], "..")) line[n++] = '/';
        line[n] = '\0';
        text(x + 10, ry + 1, line, fg);
        if (!f->dir[k]) { char sz[12]; put_size(sz, f->size[k]); text(x + wd - 10 - 8 * (int)strlen(sz), ry + 1, sz, fg); }
    }
}

/* ---- windows for ring-3 programs (v0.60.146) ---------------------------- */

/* A program's window is a slot here. The SYS_WIN_* calls run on the
 * program's task and only touch the slot (under app_mtx); this loop does
 * all the drawing: it shows a new slot as a window, repaints one whose
 * pixels changed, queues its events, and closes it when its program has
 * ended. The pixels are the program's whole body, copied in by
 * SYS_WIN_PUT; nothing is drawn from user memory. */
#define APP_MAX  8
#define APP_MAXW 640
#define APP_MAXH 480
#define APP_EVQ  64
typedef struct {
    int used, gone;                     /* gone: the desktop left; the program's calls get -1 */
    uint32_t pid;
    int w, h, win;                      /* the body's size; its window, -1 until shown */
    char title[32];
    uint32_t *px;
    int dirty;
    uwinev_t q[APP_EVQ];
    int qh, qt;
    uint32_t puts, drawn;               /* (v0.60.152) what was put, what the desktop showed */
    uint64_t draw_tsc, tsc0;            /* and what showing it cost, since tsc0 (the open) */
    uint32_t tick0;
} app_t;
static app_t app[APP_MAX];
static mutex_t app_mtx;
static int ct_running;                  /* countertop_run is in its loop (under app_mtx) */
static int app_drag = -1, app_lx, app_ly;  /* a press in a program's body, held: that window */

static uint64_t rdtsc_(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static void app_report(const app_t *a) { /* app_mtx held: what it put and what was drawn (v0.60.152) */
    uint32_t dt = timer_get_ticks() - a->tick0;
    uint64_t per_us = dt ? (rdtsc_() - a->tsc0) / ((uint64_t)dt * 10000) : 0;
    uint32_t us = per_us && a->drawn ? (uint32_t)(a->draw_tsc / a->drawn / per_us) : 0;
    klog("[countertop] %s: %u puts, %u drawn, %u us a draw\n", a->title, a->puts, a->drawn, us);
}
static void app_free(app_t *a) {        /* app_mtx held */
    if (a->used && a->win >= 0) app_report(a);
    if (a->px) kfree(a->px);
    memset(a, 0, sizeof(*a));
    a->win = -1;
}
static void app_push(int i, unsigned type, unsigned key, int x, int y, unsigned b) {
    mutex_lock(&app_mtx);
    app_t *a = &app[i];
    int nt = (a->qt + 1) % APP_EVQ;
    if (a->used && nt != a->qh) {                      /* a full queue drops the newest */
        uwinev_t *e = &a->q[a->qt];
        e->type = type; e->key = key; e->x = x; e->y = y; e->buttons = b;
        a->qt = nt;
    }
    mutex_unlock(&app_mtx);
}
static app_t *app_mine(uint32_t pid, int id) {    /* app_mtx held */
    if (id < 0 || id >= APP_MAX) return 0;
    app_t *a = &app[id];
    return a->used && !a->gone && a->pid == pid && ct_running ? a : 0;
}

int countertop_win_open(uint32_t pid, int w, int h, const char *title) {
    if (w < 1 || h < 1 || w > APP_MAXW || h > APP_MAXH) return -1;
    mutex_lock(&app_mtx);
    int id = -1;
    if (ct_running) for (int i = 0; i < APP_MAX; i++) if (!app[i].used) { id = i; break; }
    if (id >= 0) {
        app_t *a = &app[id];
        a->px = kmalloc((uint32_t)(w * h * 4));
        if (!a->px) id = -1;
        else {
            for (int i = 0; i < w * h; i++) a->px[i] = 0x00FFFFFFu;
            a->used = 1; a->gone = 0; a->pid = pid; a->w = w; a->h = h; a->win = -1; a->dirty = 1;
            a->qh = a->qt = 0;
            a->puts = a->drawn = 0; a->draw_tsc = 0; a->tsc0 = rdtsc_(); a->tick0 = timer_get_ticks();
            scopy(a->title, title, sizeof(a->title));
        }
    }
    mutex_unlock(&app_mtx);
    if (id >= 0) klog("[countertop] pid %u asks for a %dx%d window, heap %u KB free\n", pid, w, h, heap_free_bytes() / 1024);
    return id;
}
int countertop_win_size(uint32_t pid, int id, int *w, int *h) {
    mutex_lock(&app_mtx);
    app_t *a = app_mine(pid, id);
    if (a) { *w = a->w; *h = a->h; }
    mutex_unlock(&app_mtx);
    return a ? 0 : -1;
}
int countertop_win_put(uint32_t pid, int id, const uint32_t *px) {
    mutex_lock(&app_mtx);
    app_t *a = app_mine(pid, id);
    if (a) { memcpy(a->px, px, (uint32_t)(a->w * a->h * 4)); a->dirty = 1; a->puts++; }
    mutex_unlock(&app_mtx);
    return a ? 0 : -1;
}
int countertop_win_event(uint32_t pid, int id, void *ev) {
    mutex_lock(&app_mtx);
    app_t *a = app_mine(pid, id);
    int r = -1;
    if (a) {
        r = 0;
        if (a->qh != a->qt) { memcpy(ev, &a->q[a->qh], sizeof(uwinev_t)); a->qh = (a->qh + 1) % APP_EVQ; r = 1; }
    }
    mutex_unlock(&app_mtx);
    return r;
}
int countertop_win_close(uint32_t pid, int id) {
    mutex_lock(&app_mtx);
    int r = -1;
    if (id >= 0 && id < APP_MAX && app[id].used && app[id].pid == pid && app[id].gone != 2) {   /* not twice */
        if (app[id].gone || !ct_running) app_free(&app[id]);   /* no window to take down */
        else app[id].gone = 2;                                 /* the loop takes it down */
        r = 0;
    }
    mutex_unlock(&app_mtx);
    return r;
}

static void body_origin(const win_t *w, int *bx, int *by);
static void draw_app(const win_t *w) {                /* within the clip */
    int x0, y0;
    body_origin(w, &x0, &y0);
    mutex_lock(&app_mtx);
    app_t *a = &app[w->app];
    int xa = x0 < cx0 ? cx0 : x0, xb = x0 + a->w > cx1 ? cx1 : x0 + a->w;
    int ya = y0 < cy0 ? cy0 : y0, yb = y0 + a->h > cy1 ? cy1 : y0 + a->h;
    for (int j = ya; j < yb; j++) {
        const uint32_t *src = a->px + (j - y0) * a->w + (xa - x0);
        for (int i = xa; i < xb; i++) row[j][i] = *src++;
    }
    mutex_unlock(&app_mtx);
    dirty(x0, y0, a->w, a->h);
}

/* Where a window's body starts (v0.60.161): its corner, unless it is a
 * terminal or a program's window maximised, whose fixed body then sits
 * in the middle of the bigger frame. */
static void body_origin(const win_t *w, int *bx, int *by) {
    *bx = w->x + BORDER; *by = w->y + BORDER + TITLE_H;
    int iw = 0, ih = 0;
    if (w->term) { iw = w->term->cols * 8; ih = w->term->rows * 16; }
    else if (w->app >= 0) { iw = app[w->app].w; ih = app[w->app].h; }
    else return;
    int ew = w->w - 2 * BORDER - iw, eh = w->h - 2 * BORDER - TITLE_H - ih;
    if (ew > 0) *bx += ew / 2;
    if (eh > 0) *by += eh / 2;
}

static void draw_platter(const win_t *w);
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
    fill(bx - 4 - CLOSE_SZ, by, CLOSE_SZ, CLOSE_SZ, BTN_RGB);   /* the minimise box: a bar along its foot */
    fill(bx - 4 - CLOSE_SZ + 4, by + CLOSE_SZ - 5, CLOSE_SZ - 8, 2, TEXT_RGB);
    int top = w->y + BORDER + TITLE_H, bx0, by0;
    body_origin(w, &bx0, &by0);
    if ((w->term || w->app >= 0) && (w->maxed || w->snapped))   /* the frame around a fixed body */
        fill(w->x + BORDER, top, w->w - 2 * BORDER, w->h - 2 * BORDER - TITLE_H, w->term ? 0x00000000u : 0x00303438u);
    if (w->term) { draw_cells(w->term, bx0, by0, focused); return; }
    if (w->files) { draw_files(w, focused); return; }
    if (w->plat) { draw_platter(w); return; }
    if (w->app >= 0) { draw_app(w); return; }
    fill(w->x + BORDER, top, w->w - 2 * BORDER, w->h - 2 * BORDER - TITLE_H, w->body);
    for (int i = 0; i < 8 && w->lines[i]; i++) text(w->x + BORDER + 10, top + 8 + i * 18, w->lines[i], 0x00202020u);
}

/* Everything in [x,y,w,h) drawn again: the background, then the windows
 * bottom to top, each clipped to it. The bar is left alone. */
/* The Kitchen menu (v0.60.158): the programs that open windows, dropped
 * down from its button over everything else while it is open. */
#define MENU_X  BTN_KITCHEN_X
#define MENU_Y  BAR_H
#define MENU_W  240
#define MENU_IH 24
static const char *const menu_item[][3] = {      /* name, what it is, what to cook */
    { "frost", "paint", "frost.elf" },
    { "swirl", "the Mandelbrot set", "swirl.elf" },
    { "oven", "a minute's timer", "oven.elf 60" },          /* (v0.60.164) */
    { "expo", "the processes", "expo.elf" },                /* (v0.60.168) */
    { "scale", "a calculator", "scale.elf" },               /* (v0.60.173) */
    { "backdrop", "the next one", "@backdrop" },            /* (v0.60.169) not programs: '@' an action */
    { "cloche", "lock the desktop", "@cloche" },            /* (v0.60.176) */
    { "close up", "power off", "@close" },                  /* (v0.60.183) the headchef's */
    { "reheat", "restart", "@reheat" },
};
#define MENU_N (int)(sizeof(menu_item) / sizeof(menu_item[0]))
#define MENU_H (MENU_N * MENU_IH + 4)
static int menu_open;
/* A files window's right-click menu (v0.60.172): "new bowl" in the bowl it
 * shows, and on a row "remove NAME", which asks again on the first click
 * (as Leave does) and removes on the second. The shell's rules decide:
 * users_may 'w' on the bowl for a new one or a bowl removed, on the file
 * for a file; a bowl that is not empty is not removed. */
#define CTX_W  260
#define CTX_IH 22
/* A menu on the background (v0.60.184): a right click where there is no
 * window, icon or bar drops it there. A click on an entry does what its
 * button does; a click anywhere else, or Esc, puts it up. */
static const char *const desk_item[] = { "Terminal", "Files", "Kitchen", "backdrop", "cloche" };
#define DESK_N 5
#define DESK_W 150
#define DESK_IH 24
static int desk_open, desk_x, desk_y;
static void draw_desk(void) {                      /* within the clip */
    int h = DESK_N * DESK_IH + 4;
    fill(desk_x, desk_y, DESK_W, h, EDGE_RGB);
    fill(desk_x + 1, desk_y + 1, DESK_W - 2, h - 2, 0x00F4F1EAu);
    for (int i = 0; i < DESK_N; i++) text(desk_x + 10, desk_y + 2 + i * DESK_IH + (DESK_IH - 16) / 2, desk_item[i], 0x00202020u);
}
static int ctx_win = -1, ctx_row = -1, ctx_x, ctx_y, ctx_armed;
static int ctx_bmp(void);
static int ctx_items(void) { return ctx_row < 0 ? 1 : ctx_bmp() ? 3 : 2; }
static void draw_ctx(void) {                       /* within the clip */
    int h = ctx_items() * CTX_IH + 4;
    fill(ctx_x, ctx_y, CTX_W, h, EDGE_RGB);
    fill(ctx_x + 1, ctx_y + 1, CTX_W - 2, h - 2, 0x00F4F1EAu);
    text(ctx_x + 10, ctx_y + 2 + (CTX_IH - 16) / 2, "new bowl", 0x00202020u);
    if (ctx_row >= 0 && win[ctx_win].files) {
        char line[96] = "remove ";
        if (ctx_armed) scopy(line, "really remove ", (int)sizeof(line));
        int l = (int)strlen(line);
        scopy(line + l, win[ctx_win].files->name[ctx_row], (int)sizeof(line) - l - 2);
        if (ctx_armed) strcat(line, "?");
        text(ctx_x + 10, ctx_y + 2 + CTX_IH + (CTX_IH - 16) / 2, line, ctx_armed ? 0x00C0504Du : 0x00202020u);
        if (ctx_items() == 3) text(ctx_x + 10, ctx_y + 2 + 2 * CTX_IH + (CTX_IH - 16) / 2, "use as background", 0x00202020u);
    }
}
static void draw_menu(void) {                     /* within the clip */
    fill(MENU_X, MENU_Y, MENU_W, MENU_H, EDGE_RGB);
    fill(MENU_X + 1, MENU_Y, MENU_W - 2, MENU_H - 1, 0x00F4F1EAu);
    for (int i = 0; i < MENU_N; i++) {
        int y = MENU_Y + 2 + i * MENU_IH + (MENU_IH - 16) / 2;
        text(MENU_X + 10, y, menu_item[i][0], 0x00202020u);
        text(MENU_X + 10 + 8 * 8, y, menu_item[i][1], 0x00606870u);
    }
}
/* Icons on the background (v0.60.174): the Kitchen menu's programs down
 * the left, a coloured square with the program's initial and its name
 * under it. Drawn with the background, so windows cover them; a double
 * click opens one as the menu does. */
#define ICON_X  20
#define ICON_Y0 40
#define ICON_DY 76
#define ICON_SZ 40
static const uint32_t icon_rgb[] = { 0x00C0392Bu, 0x002E86DEu, 0x00E67E22u, 0x0027AE60u, 0x008E44ADu };
static int icon_count(void) {                      /* the menu's programs, not its backdrop */
    int n = 0;
    for (int i = 0; i < MENU_N; i++) if (menu_item[i][2][0] != '@') n++;
    return n;
}
static void big_char(int x, int y, char ch, uint32_t fg) {   /* the font, twice the size */
    const uint8_t *g = font8x16[(uint8_t)ch];
    for (int j = 0; j < 32; j++)
        for (int i = 0; i < 16; i++)
            if ((g[j / 2] >> (7 - i / 2)) & 1) fill(x + i, y + j, 1, 1, fg);
}
static void draw_icons(void) {                     /* within the clip */
    for (int i = 0; i < icon_count(); i++) {
        int y = ICON_Y0 + i * ICON_DY;
        fill(ICON_X, y, ICON_SZ, ICON_SZ, icon_rgb[i % 5]);
        big_char(ICON_X + (ICON_SZ - 16) / 2, y + (ICON_SZ - 32) / 2, (char)(menu_item[i][0][0] - 32), TEXT_RGB);
        int l = (int)strlen(menu_item[i][0]);
        text(ICON_X + ICON_SZ / 2 - 4 * l, y + ICON_SZ + 4, menu_item[i][0], TEXT_RGB);
    }
}
static int icon_at(int px, int py) {               /* the icon (its square or its name) under a point, or -1 */
    for (int i = 0; i < icon_count(); i++) {
        int y = ICON_Y0 + i * ICON_DY;
        if (px >= ICON_X - 12 && px < ICON_X + ICON_SZ + 12 && py >= y && py < y + ICON_SZ + 22) return i;
    }
    return -1;
}

static int ct_uid;                                 /* whose desktop (v0.60.143) */
/* The cook's own time (v0.60.187): the RTC is UTC (QEMU's default), so
 * "zone N" in the settings (hours from UTC, -12 to +14) shifts it, the
 * day, month and year with it (through days since 1970), and "clock 12"
 * shows the bar's clock as " 9:05 pm" (no seconds: the same 8 places). */
static int zone_h, clock12;
static int32_t days_from_civil(int y, int m, int d) {   /* Hinnant's */
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void civil_from_days(int32_t z, int *y, int *m, int *d) {
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1; *m = mp + (mp < 10 ? 3 : -9); *y = yoe + era * 400 + (*m <= 2);
}
static void local_now(rtc_time_t *t) {
    rtc_read(t);
    if (!zone_h) return;
    int32_t h = days_from_civil(t->year, t->month, t->day) * 24 + t->hour + zone_h;
    int32_t days = h >= 0 ? h / 24 : (h - 23) / 24;
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    t->year = (uint16_t)y; t->month = (uint8_t)m; t->day = (uint8_t)d; t->hour = (uint8_t)(h - days * 24);
}
static int clock_sec;                            /* (with the bar, below) */
/* A calendar under the clock (v0.60.175): this month from the RTC, today
 * on blue; a click on the clock drops it and puts it up again, a click
 * anywhere else puts it up. */
static void repaint(int x, int y, int w, int h);
#define CAL_W 184
#define CAL_H 158
#define CAL_X (W - CAL_W - 4)
#define CAL_Y BAR_H
static int cal_open;
static rtc_time_t cal_t;
static int weekday(int y, int m, int d) {          /* 0 Sunday (Sakamoto's) */
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
static int month_days(int y, int m) {
    static const int n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 29 : n[m - 1];
}
static void draw_cal(void) {                       /* within the clip */
    static const char *const mon[12] = { "January", "February", "March", "April", "May", "June", "July",
                                         "August", "September", "October", "November", "December" };
    fill(CAL_X, CAL_Y, CAL_W, CAL_H, EDGE_RGB);
    fill(CAL_X + 1, CAL_Y, CAL_W - 2, CAL_H - 1, 0x00F4F1EAu);
    char title[24];
    scopy(title, mon[(cal_t.month + 11) % 12], (int)sizeof(title));
    int l = (int)strlen(title);
    title[l++] = ' ';
    for (int v = 1000; v; v /= 10) title[l++] = (char)('0' + cal_t.year / v % 10);
    title[l] = '\0';
    text(CAL_X + 8, CAL_Y + 4, title, 0x00202020u);
    text(CAL_X + 8, CAL_Y + 24, "Su Mo Tu We Th Fr Sa", 0x00606870u);
    int first = weekday(cal_t.year, cal_t.month, 1), n = month_days(cal_t.year, cal_t.month);
    for (int d = 1; d <= n; d++) {
        int cell = first + d - 1, x = CAL_X + 8 + (cell % 7) * 24, y = CAL_Y + 44 + (cell / 7) * 18;
        char num[3] = { (char)(d < 10 ? ' ' : '0' + d / 10), (char)('0' + d % 10), 0 };
        int today = d == cal_t.day;
        if (today) fill(x - 2, y - 1, 20, 18, FOCUS_RGB);
        text(x, y, num, today ? TEXT_RGB : 0x00202020u);
    }
}
/* The cloche (v0.60.176): the desktop locked under a dome. It covers the
 * whole screen (drawn over anything repainted while it is down; the bar's
 * clock and the taskbar wait); keys go to it alone; the cook's secret,
 * checked by users_check, lifts it. Programs go on under it. Wrong secrets
 * are logged; after three it waits 3 s before listening again. */
static int locked, lock_len, lock_fails;
/* Clocking in on the desktop (v0.60.179): the cloche's screen with two
 * fields, the cook's name and then their secret, checked by the shell's
 * callback, which does what a login does; until then nobody's desktop. */
static uint32_t last_input;                      /* (v0.60.176) for the idle cloche */
static int (*login_cb)(const char *name, const char *secret);
static int login_mode, login_len;
static char login_name[16];
static char lock_buf[32], lock_msg[48];
static uint32_t lock_wait;
static void draw_lock(void) {                      /* within the clip */
    fill(cx0, cy0, cx1 - cx0, cy1 - cy0, 0x00101418u);
    if (login_mode) {
        char a[40] = "cook:   ", b[40] = "secret: ";
        int l = 8;
        for (int i = 0; i < login_len; i++) a[l++] = login_name[i];
        if (login_mode == 1) a[l++] = '_';
        a[l] = '\0';
        l = 8;
        for (int i = 0; i < lock_len && l < 38; i++) b[l++] = '*';
        if (login_mode == 2) b[l++] = '_';
        b[l] = '\0';
        text(W / 2 - 8 * 7, H / 2 - 90, "soupOS kitchen", 0x00F0B040u);
        text(W / 2 - 4 * 28, H / 2 - 60, "clock in to start your shift", TEXT_RGB);
        text(W / 2 - 8 * 10, H / 2 - 20, a, TEXT_RGB);
        text(W / 2 - 8 * 10, H / 2 + 4, b, TEXT_RGB);
        if (lock_msg[0]) text(W / 2 - 4 * (int)strlen(lock_msg), H / 2 + 40, lock_msg, 0x00F05050u);
        return;
    }
    const char *who = users_name_of((uint8_t)ct_uid);
    char line[64] = "secret: ";
    int l = 8;
    for (int i = 0; i < lock_len && l < 40; i++) line[l++] = '*';
    line[l] = '\0';
    text(W / 2 - 8 * 11, H / 2 - 60, "under the cloche", TEXT_RGB);
    text(W / 2 - 4 * (int)strlen(who), H / 2 - 30, who, 0x00F0B040u);
    text(W / 2 - 8 * 10, H / 2, line, TEXT_RGB);
    if (lock_msg[0]) text(W / 2 - 4 * (int)strlen(lock_msg), H / 2 + 30, lock_msg, 0x00F05050u);
}
static void cloche_lock(const char *why) {
    if (locked) return;
    locked = 1; lock_len = 0; lock_fails = 0; lock_msg[0] = '\0';
    cx0 = 0; cy0 = 0; cx1 = W; cy1 = H;
    draw_lock();
    klog("[countertop] cloche down (%s)\n", why);
}
static void bar(void);
static void taskbar(void);
static void load_settings(void);
static void draw_icons(void);
static void login_key(int c) {
    if (login_mode == 1) {                             /* the name */
        if (c == '\b') { if (login_len) login_len--; }
        else if (c == '\n') { if (login_len) { login_mode = 2; lock_len = 0; } }
        else if (c > ' ' && c < 127 && login_len < (int)sizeof(login_name) - 1) login_name[login_len++] = (char)c;
        cx0 = 0; cy0 = 0; cx1 = W; cy1 = H;
        draw_lock();
        return;
    }
    if (c == '\b') { if (lock_len) lock_len--; }
    else if (c == '\n') {
        login_name[login_len] = '\0'; lock_buf[lock_len] = '\0';
        int uid = login_cb(login_name, lock_buf);
        memset(lock_buf, 0, sizeof(lock_buf)); lock_len = 0;
        if (uid >= 0) {
            ct_uid = uid; login_mode = 0; locked = 0;
            klog("[countertop] clocked in %s\n", login_name);
            login_len = 0; lock_msg[0] = '\0';
            load_settings();                           /* the cook's background */
            repaint(0, BAR_H, W, H - BAR_H - TASK_H);
            bar(); taskbar();
            last_input = timer_get_ticks();
            return;
        }
        lock_fails++;
        klog("[countertop] clock-in refused (%d)\n", lock_fails);
        scopy(lock_msg, "wrong cook or secret", (int)sizeof(lock_msg));
        login_mode = 1; login_len = 0;
        if (lock_fails % 3 == 0) { lock_wait = timer_get_ticks() + 300; scopy(lock_msg, "wait a moment", (int)sizeof(lock_msg)); }
    } else if (c >= ' ' && c < 127 && lock_len < (int)sizeof(lock_buf) - 1) lock_buf[lock_len++] = (char)c;
    cx0 = 0; cy0 = 0; cx1 = W; cy1 = H;
    draw_lock();
}
static void cloche_key(int c) {
    if (lock_wait && timer_get_ticks() < lock_wait) return;   /* after three wrong: waiting */
    lock_wait = 0;
    if (login_mode) { login_key(c); return; }
    if (c == '\b') { if (lock_len) lock_len--; }
    else if (c == '\n') {
        lock_buf[lock_len] = '\0';
        if (users_check(users_name_of((uint8_t)ct_uid), lock_buf) >= 0) {
            locked = 0;
            memset(lock_buf, 0, sizeof(lock_buf)); lock_len = 0;
            klog("[countertop] cloche lifted\n");
            repaint(0, BAR_H, W, H - BAR_H - TASK_H);
            bar(); taskbar();
            return;
        }
        lock_fails++;
        memset(lock_buf, 0, sizeof(lock_buf)); lock_len = 0;
        klog("[countertop] cloche: a wrong secret (%d)\n", lock_fails);
        scopy(lock_msg, "that is not the secret", (int)sizeof(lock_msg));
        if (lock_fails % 3 == 0) { lock_wait = timer_get_ticks() + 300; scopy(lock_msg, "wait a moment", (int)sizeof(lock_msg)); }
    } else if (c >= ' ' && c < 127 && lock_len < (int)sizeof(lock_buf) - 1) lock_buf[lock_len++] = (char)c;
    cx0 = 0; cy0 = 0; cx1 = W; cy1 = H;
    draw_lock();
}
static void cal_set(int open) {
    if (cal_open == open) return;
    cal_open = open;
    if (open) local_now(&cal_t);
    repaint(CAL_X, CAL_Y, CAL_W, CAL_H);
    if (open) klog("[countertop] calendar %u-%u, today %u\n", cal_t.year, cal_t.month, cal_t.day);
}

static void repaint(int x, int y, int w, int h) {
    cx0 = x < 0 ? 0 : x; cy0 = y < BAR_H ? BAR_H : y;
    cx1 = x + w > W ? W : x + w; cy1 = y + h > H - TASK_H ? H - TASK_H : y + h;
    if (cx0 < cx1 && cy0 < cy1) {
        background();
        draw_icons();
        for (int i = 0; i < nwin; i++) if (!win[order[i]].min) draw_window(order[i]);
        if (menu_open) draw_menu();
        if (ctx_win >= 0) draw_ctx();
        if (desk_open) draw_desk();
        if (cal_open) draw_cal();
        if (locked) draw_lock();                       /* the cloche over everything (v0.60.176) */
    }
    clip_all();
}
static void settings_path(char *p) {               /* the cook's `countertop` file */
    if (ct_uid == 0) { strcpy(p, "/countertop"); return; }
    strcpy(p, "/home/");
    strncpy(p + 6, users_name_of((uint8_t)ct_uid), FAT_PATH_MAX - 20);
    p[FAT_PATH_MAX - 14] = '\0';
    strcat(p, "/countertop");
}
static uint32_t idle_s = 600;                      /* (v0.60.176) the cloche after this long; 0 never */
static void pic_free(void) {
    for (int y = 0; y < pic_h; y++) { if (pic_row[y]) pmm_free_page(pic_row[y]); pic_row[y] = 0; }
    pic_w = pic_h = 0;
}
static int read_all(int fd, uint8_t *b, uint32_t n) {
    for (uint32_t got = 0; got < n; ) {
        int r = fat_fread(fd, b + got, n - got);
        if (r <= 0) return -1;
        got += (uint32_t)r;
    }
    return 0;
}
static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
/* A 24-bit uncompressed BMP into pages, a row a page (v0.60.182; shared
 * with the platter, v0.60.186): 0, or why not. rows[] has MAX_H entries;
 * on failure none of them is left allocated. */
static const char *bmp_read(const char *path, uint32_t **rows, int *pw, int *ph) {
    static uint8_t buf[1024 * 3 + 4];
    const char *why = 0;
    int fd = -1, w = 0, h = 0, up = 1;
    uint8_t hd[54];
    if (!users_may(path, 'r')) why = "not yours to read";
    else if ((fd = fat_fopen(path)) < 0) why = "not there";
    else if (read_all(fd, hd, 54) < 0) why = "not a 24-bit BMP";
    else {
        uint32_t off = le32(hd + 10);
        w = (int)le32(hd + 18); h = (int)le32(hd + 22);
        if (h < 0) { h = -h; up = 0; }                 /* top-down rows */
        if (hd[0] != 'B' || hd[1] != 'M' || hd[28] != 24 || hd[29] || le32(hd + 30) || off < 54) why = "not a 24-bit BMP";
        else if (w <= 0 || h <= 0 || w > W || h > H) why = "bigger than the screen";
        for (uint32_t skip = off - 54; !why && skip; ) {   /* a longer header: past it */
            uint32_t k = skip > sizeof(buf) ? sizeof(buf) : skip;
            if (read_all(fd, buf, k) < 0) why = "not a 24-bit BMP";
            skip -= k;
        }
    }
    uint32_t stride = ((uint32_t)w * 3 + 3) & ~3u;
    for (int i = 0; !why && i < h; i++) {
        if (read_all(fd, buf, stride) < 0) { why = "cut short"; break; }
        int y = up ? h - 1 - i : i;
        if (!(rows[y] = (uint32_t *)pmm_alloc_page())) { why = "no memory for it"; break; }
        for (int x = 0; x < w; x++) rows[y][x] = (uint32_t)buf[x * 3 + 2] << 16 | (uint32_t)buf[x * 3 + 1] << 8 | buf[x * 3];
    }
    if (fd >= 0) fat_fclose(fd);
    if (why) {
        for (int y = 0; y < h && y < MAX_H; y++) if (rows[y]) { pmm_free_page(rows[y]); rows[y] = 0; }
        return why;
    }
    *pw = w; *ph = h;
    return 0;
}
static void pic_load(void) {                       /* pic_path, read as the cook may */
    pic_free();
    if (!pic_path[0]) return;
    uint32_t t0 = timer_get_ticks();
    int w, h;
    const char *why = bmp_read(pic_path, pic_row, &w, &h);
    if (why) {
        klog("[countertop] picture %s: %s, the gradient instead\n", pic_path, why);
        return;
    }
    pic_w = w; pic_h = h;
    uint32_t t1 = timer_get_ticks();
    clip_all();                                        /* what drawing it costs: 20 whole backgrounds */
    for (int i = 0; i < 20; i++) background();
    uint32_t t2 = timer_get_ticks();
    klog("[countertop] picture %s: %dx%d, read in %u ms, drawn in %u.%u ms\n", pic_path, w, h,
         (t1 - t0) * 10, (t2 - t1) * 10 / 20, (t2 - t1) * 10 % 20 * 10 / 20);
}
static void load_settings(void) {
    char p[FAT_PATH_MAX], buf[384];
    uint32_t n = 0;
    settings_path(p);
    bg_choice = 0; idle_s = 600; pic_path[0] = '\0'; zone_h = 0; clock12 = 0;
    pic_free();
    if (fat_read(p, (uint8_t *)buf, sizeof(buf) - 1, &n) < 0) return;
    buf[n < sizeof(buf) - 1 ? n : sizeof(buf) - 1] = '\0';
    int idle_set = 0;
    for (char *l = buf; *l; ) {                        /* a setting a line */
        if (!strncmp(l, "background ", 11) && l[11] >= '0' && l[11] < '0' + NBG) bg_choice = l[11] - '0';
        if (!strncmp(l, "idle ", 5)) {
            uint32_t v = 0;
            for (char *d = l + 5; *d >= '0' && *d <= '9'; d++) v = v * 10 + (uint32_t)(*d - '0');
            idle_s = v; idle_set = 1;
        }
        if (!strncmp(l, "zone ", 5)) {                 /* (v0.60.187) */
            int neg = l[5] == '-', v = 0;
            for (char *d = l + 5 + (neg || l[5] == '+'); *d >= '0' && *d <= '9'; d++) v = v * 10 + (*d - '0');
            if (neg) v = -v;
            if (v >= -12 && v <= 14) zone_h = v;
        }
        if (!strncmp(l, "clock 12", 8)) clock12 = 1;
        if (!strncmp(l, "picture ", 8)) {              /* the rest of the line, spaces and all */
            int k = 0;
            for (char *d = l + 8; *d && *d != '\n' && *d != '\r' && k < FAT_PATH_MAX - 1; d++) pic_path[k++] = *d;
            pic_path[k] = '\0';
        }
        while (*l && *l != '\n') l++;
        if (*l) l++;
    }
    klog("[countertop] settings from %s: background %s\n", p, bgs[bg_choice].name);
    if (idle_set) klog("[countertop] settings: the cloche after %u s idle\n", idle_s);
    if (zone_h || clock12) klog("[countertop] settings: zone %d, %d-hour clock\n", zone_h, clock12 ? 12 : 24);
    clock_sec = -1;                                    /* the clock again, in this cook's time */
    pic_load();
}
static int save_settings(char *p);
static void next_background(void) {
    bg_choice = (bg_choice + 1) % NBG;
    repaint(0, BAR_H, W, H - BAR_H - TASK_H);
    char p[FAT_PATH_MAX];
    int ok = save_settings(p);
    klog("[countertop] background %s, %s %s\n", bgs[bg_choice].name, ok ? "kept in" : "not kept in", p);
}
static int save_settings(char *p) {                /* the cook's settings file, all of them (v0.60.186) */
    char line[40 + FAT_PATH_MAX] = "background 0\n";
    line[11] = (char)('0' + bg_choice);
    if (idle_s != 600) {                               /* the other setting kept (v0.60.176) */
        strcat(line, "idle ");
        char d[12]; int k = 0; uint32_t v = idle_s;
        do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        int l = (int)strlen(line);
        while (k) line[l++] = d[--k];
        line[l++] = '\n'; line[l] = '\0';
    }
    if (pic_path[0]) { strcat(line, "picture "); strcat(line, pic_path); strcat(line, "\n"); }   /* kept (v0.60.182) */
    if (zone_h) {                                      /* (v0.60.187) */
        int v = zone_h < 0 ? -zone_h : zone_h, l = (int)strlen(line);
        strcpy(line + l, zone_h < 0 ? "zone -" : "zone "); l = (int)strlen(line);
        if (v >= 10) line[l++] = (char)('0' + v / 10);
        line[l++] = (char)('0' + v % 10); line[l++] = '\n'; line[l] = '\0';
    }
    if (clock12) strcat(line, "clock 12\n");
    settings_path(p);
    return users_may(p, 'w') && fat_write(p, (const uint8_t *)line, (uint32_t)strlen(line)) >= 0;
}
static void set_picture(const char *path) {       /* the files menu's "use as background" (v0.60.186) */
    scopy(pic_path, path, (int)sizeof(pic_path));
    pic_load();
    repaint(0, BAR_H, W, H - BAR_H - TASK_H);
    char p[FAT_PATH_MAX];
    int ok = pic_h && save_settings(p);
    if (!pic_h) pic_path[0] = '\0';
    klog("[countertop] picture %s %s %s\n", path, ok ? "kept in" : "not kept in", ok ? p : "the settings");
}
static int open_terminal(int hidden);
/* A program from the Kitchen menu or an icon: a hidden terminal (v0.60.178:
 * never drawn, not on the taskbar) cooks it and then clocks out, so only the
 * program's own window shows, and the terminal goes when the program does. */
static void kitchen_launch(int i) {
    int t = open_terminal(1);
    if (t < 0) return;
    char line[48] = "cook ";
    scopy(line + 5, menu_item[i][2], (int)sizeof(line) - 16);   /* room for the clockout */
    strcat(line, "\nclockout\n");
    for (const char *c = line; *c; c++) wt_key(win[t].term, *c);
    klog("[countertop] kitchen: %s\n", menu_item[i][0]);
}
static void menu_set(int open) {
    if (menu_open == open) return;
    menu_open = open;
    repaint(MENU_X, MENU_Y, MENU_W, MENU_H);
    klog("[countertop] kitchen menu %s\n", open ? "open" : "closed");
}
static void repaint_win(int k) { repaint(win[k].x, win[k].y, win[k].w, win[k].h); }

static int open_hidden;                          /* the next window opens hidden (v0.60.178) */
static int open_window(int x, int y, int w, int h, const char *title, uint32_t body, const char *const *lines) {
    for (int k = 0; k < MAX_WIN; k++) if (!win[k].used) {
        win_t *v = &win[k];
        v->used = 1; v->x = x; v->y = y; v->w = w; v->h = h; v->body = body; v->term = 0; v->sess = 0; v->min = 0; v->files = 0; v->app = -1; v->maxed = 0; v->snapped = 0;
        v->hidden = v->min = open_hidden; v->plat = 0;              /* hidden: minimised for good, and off the taskbar */
        scopy(v->title, title, sizeof(v->title));
        for (int i = 0; i < 8; i++) v->lines[i] = lines && lines[i] ? lines[i] : 0;
        int was = topmost();
        order[nwin++] = k;
        if (was >= 0 && !open_hidden) repaint_win(was);   /* its title bar goes grey */
        repaint_win(k);
        return k;
    }
    return -1;
}
static void close_window(int k);
static int nterm;
static int open_terminal(int hidden) {
    wterm_t *t = wt_new();
    if (!t) return -1;
    static const char *const none[] = { 0 };
    int ww = TCOLS * 8 + 2 * BORDER, wh = TROWS * 16 + 2 * BORDER + TITLE_H;
    int off = (nterm % 6) * 28;
    char title[32] = "kitchen";                        /* a hidden one is not counted */
    if (!hidden) {
        strcpy(title, "Terminal ");
        int n = ++nterm, l = 9;
        if (n >= 10) title[l++] = (char)('0' + n / 10 % 10);
        title[l++] = (char)('0' + n % 10); title[l] = '\0';
    }
    open_hidden = hidden;
    int k = open_window(60 + off, 60 + off, ww, wh, title, 0, none);
    open_hidden = 0;
    if (k < 0) { kfree(t); return -1; }
    win[k].term = t;
    win[k].sess = shell_session_start(&t->t, ct_uid, 0, "countertop");
    if (!win[k].sess) { win[k].term = 0; close_window(k); kfree(t); return -1; }
    klog("[countertop] open %s\n", win[k].title);
    repaint_win(k);
    return k;
}

static int open_files(const char *path) {
    struct files *f = kmalloc(sizeof(struct files));
    if (!f) return -1;
    memset(f, 0, sizeof(*f));
    strncpy(f->path, path, FAT_PATH_MAX - 1);
    files_load(f);
    static const char *const none[] = { 0 };
    int n = 0;                                         /* each new one a step down and left (v0.60.177) */
    for (int i = 0; i < MAX_WIN; i++) if (win[i].used && win[i].files) n++;
    int k = open_window(W - 420 - 28 * n, 60 + 28 * n, 380, 460, "Files", 0, none);
    if (k < 0) { kfree(f); return -1; }
    win[k].files = f;
    klog("[countertop] open Files at %s\n", f->path);
    repaint_win(k);
    return k;
}
/* A text file, by its name: .txt .md .sc .sh, any case (v0.60.154). */
static int is_text(const char *p) {
    const char *dot = 0;
    for (const char *c = p; *c; c++) if (*c == '.') dot = c; else if (*c == '/') dot = 0;
    if (!dot) return 0;
    static const char *const ext[] = { "txt", "md", "sc", "sh" };
    for (unsigned i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) {
        const char *a = dot + 1, *b = ext[i];
        while (*a && *b && (*a | 0x20) == *b) { a++; b++; }
        if (!*a && !*b) return 1;
    }
    return 0;
}
/* Open what row r of files window k names: a bowl here, a file in a new
 * terminal with `jot 'PATH'` typed into it for text, `pour 'PATH'` for the
 * rest. */
/* Row r's path: the bowl above for "..". 0, or -1 if it will not fit. */
static int files_path(const struct files *f, int r, char p[FAT_PATH_MAX]) {
    if (!strcmp(f->name[r], "..")) {
        strcpy(p, f->path);
        int l = (int)strlen(p);
        while (l > 1 && p[l - 1] != '/') l--;
        p[l > 1 ? l - 1 : 1] = '\0';
        return 0;
    }
    int l = (int)strlen(f->path);
    if (l + 1 + (int)strlen(f->name[r]) >= FAT_PATH_MAX) return -1;
    strcpy(p, f->path);
    if (l > 1) p[l++] = '/';
    strcpy(p + l, f->name[r]);
    return 0;
}
/* A path typed into a terminal, single-quoted (a ' inside as '\''), as the
 * shell reads it back: names have spaces. */
static void type_path(wterm_t *t, const char *p) {
    wt_key(t, '\'');
    for (const char *c = p; *c; c++) {
        if (*c == '\'') { wt_key(t, '\''); wt_key(t, '\\'); wt_key(t, '\''); }
        wt_key(t, *c);
    }
    wt_key(t, '\'');
}
static void desk_set(int open, int x, int y) {
    if (desk_open) { desk_open = 0; repaint(desk_x, desk_y, DESK_W, DESK_N * DESK_IH + 4); }
    if (!open) return;
    desk_x = x + DESK_W > W ? W - DESK_W : x;
    desk_y = y + DESK_N * DESK_IH + 4 > H - TASK_H ? H - TASK_H - DESK_N * DESK_IH - 4 : y;
    desk_open = 1;
    repaint(desk_x, desk_y, DESK_W, DESK_N * DESK_IH + 4);
    klog("[countertop] background menu at %d,%d\n", desk_x, desk_y);
}
static void ctx_close(void) {
    if (ctx_win < 0) return;
    int h = ctx_items() * CTX_IH + 4;
    ctx_win = -1; ctx_armed = 0;
    repaint(ctx_x, ctx_y, CTX_W, h);
}
static void ctx_open(int k, int r, int x, int y) {
    ctx_close();
    ctx_win = k; ctx_row = r; ctx_armed = 0;
    ctx_x = x + CTX_W > W ? W - CTX_W : x;
    ctx_y = y + ctx_items() * CTX_IH + 4 > H - TASK_H ? H - TASK_H - ctx_items() * CTX_IH - 4 : y;
    repaint(ctx_x, ctx_y, CTX_W, ctx_items() * CTX_IH + 4);
}
static void ctx_new_bowl(struct files *f) {
    char p[FAT_PATH_MAX];
    int l = (int)strlen(f->path);
    for (int n = 1; n < 100; n++) {                    /* bowl, bowl2, bowl3 ... */
        strcpy(p, f->path);
        if (l > 1) strcat(p, "/");
        strcat(p, "bowl");
        if (n > 1) { char d[4] = { (char)('0' + n / 10), (char)('0' + n % 10), 0, 0 }; strcat(p, n < 10 ? d + 1 : d); }
        if (!fat_exists(p)) break;
    }
    if (!users_may(f->path, 'w')) { klog("[countertop] may not make %s\n", p); return; }
    klog("[countertop] %s %s\n", fat_mkdir(p) == 0 ? "new bowl" : "could not make", p);
}
static void ctx_remove(struct files *f, int r) {
    char p[FAT_PATH_MAX];
    if (files_path(f, r, p) < 0) return;
    int bowl = f->dir[r];
    if (!users_may(bowl ? f->path : p, 'w')) { klog("[countertop] may not remove %s\n", p); return; }
    int ok = bowl ? fat_rmdir(p) : fat_delete(p);
    klog("[countertop] %s %s\n", ok == 0 ? "removed" : bowl ? "not empty, kept" : "could not remove", p);
}
/* A click with the menu up: 1 if it was on the menu. */
static int ctx_click(int px, int py) {
    int h = ctx_items() * CTX_IH + 4;
    if (px < ctx_x || px >= ctx_x + CTX_W || py < ctx_y || py >= ctx_y + h) { ctx_close(); return 0; }
    int i = (py - ctx_y - 2) / CTX_IH, k = ctx_win;
    struct files *f = win[k].files;
    if (!f) { ctx_close(); return 1; }
    if (i == 1 && ctx_row >= 0 && !ctx_armed) {        /* remove: ask again */
        ctx_armed = 1;
        repaint(ctx_x, ctx_y, CTX_W, h);
        return 1;
    }
    if (i == 0) ctx_new_bowl(f);
    else if (i == 1 && ctx_row >= 0) ctx_remove(f, ctx_row);
    else if (i == 2 && ctx_bmp()) {                    /* (v0.60.186) */
        char path[FAT_PATH_MAX];
        if (files_path(f, ctx_row, path) == 0) set_picture(path);
    }
    ctx_close();
    files_load(f);
    repaint_win(k);
    return 1;
}

/* A copy between files windows (v0.60.177), streamed through fat_write_from
 * from an open fd: no whole-file buffer, so no size limit but the disk's. */
struct copy_src { int fd; uint32_t pos; };
static int copy_pull(void *ctx, uint32_t off, uint8_t *dst, uint32_t n) {
    struct copy_src *c = ctx;
    if (off != c->pos) return -1;                      /* fat_write_from pulls in order */
    uint32_t got = 0;
    while (got < n) {
        int r = fat_fread(c->fd, dst + got, n - got);
        if (r <= 0) return -1;
        got += (uint32_t)r;
    }
    c->pos += n;
    return 0;
}
static void files_copy(int from, int r, int to) {
    struct files *f = win[from].files, *t = win[to].files;
    char src[FAT_PATH_MAX], dst[FAT_PATH_MAX];
    if (r < 0 || r >= f->n || files_path(f, r, src) < 0) return;
    if (f->dir[r]) { klog("[countertop] only files are copied, not %s\n", src); return; }
    int l = (int)strlen(t->path);
    const char *prefix = "";
    for (int tries = 0; tries < 3; tries++) {          /* a name taken there: "copy " in front */
        if (l + 1 + (int)strlen(prefix) + (int)strlen(f->name[r]) >= FAT_PATH_MAX) return;
        strcpy(dst, t->path);
        if (l > 1) strcat(dst, "/");
        strcat(dst, prefix);
        strcat(dst, f->name[r]);
        if (!fat_exists(dst)) break;
        prefix = tries == 0 ? "copy " : "copy copy ";
    }
    if (fat_exists(dst)) { klog("[countertop] %s is taken there\n", f->name[r]); return; }
    if (!users_may(src, 'r') || !users_may(t->path, 'w')) { klog("[countertop] may not copy %s to %s\n", src, dst); return; }
    struct copy_src c;
    c.fd = fat_fopen(src); c.pos = 0;
    if (c.fd < 0) { klog("[countertop] could not open %s\n", src); return; }
    uint32_t size = fat_fsize(c.fd);
    int ok = fat_write_from(dst, size, copy_pull, &c) == 0;
    fat_fclose(c.fd);
    klog("[countertop] %s %s to %s (%u bytes)\n", ok ? "copied" : "could not copy", src, dst, size);
    files_load(t);
    repaint_win(to);
}
/* A platter (v0.60.186): a .bmp double-clicked in a files window, in a
 * window of its own at its own size up to most of the screen; the wheel
 * scrolls a taller one. Its rows are pages, freed with the window. */
struct platter { uint32_t *row[MAX_H]; int w, h, top; };
static void bar_note(const char *msg);
static int note_showing;                         /* with the notes, below */
static uint32_t note_at;
static int is_bmp(const char *p) {
    int l = (int)strlen(p);
    return l > 4 && p[l - 4] == '.' && (p[l - 3] | 0x20) == 'b' && (p[l - 2] | 0x20) == 'm' && (p[l - 1] | 0x20) == 'p';
}
static int ctx_bmp(void) {                         /* the files menu's row is a picture */
    return ctx_win >= 0 && ctx_row >= 0 && win[ctx_win].files && !win[ctx_win].files->dir[ctx_row] && is_bmp(win[ctx_win].files->name[ctx_row]);
}
static int plat_view_h(const win_t *w) { return w->h - 2 * BORDER - TITLE_H; }
static void draw_platter(const win_t *w) {        /* within the clip */
    struct platter *p = w->plat;
    int x0 = w->x + BORDER, y0 = w->y + BORDER + TITLE_H, bw = w->w - 2 * BORDER, bh = plat_view_h(w);
    fill(x0, y0, bw, bh, 0x00202428u);                 /* past the picture */
    int xa = x0 < cx0 ? cx0 : x0, xb = x0 + (p->w < bw ? p->w : bw);
    if (xb > cx1) xb = cx1;
    int ya = y0 < cy0 ? cy0 : y0, yb = y0 + (p->h - p->top < bh ? p->h - p->top : bh);
    if (yb > cy1) yb = cy1;
    for (int j = ya; j < yb; j++) {
        const uint32_t *src = p->row[p->top + j - y0] - x0;
        for (int i = xa; i < xb; i++) row[j][i] = src[i];
    }
}
static void plat_free(win_t *w) {
    if (!w->plat) return;
    for (int y = 0; y < w->plat->h; y++) if (w->plat->row[y]) pmm_free_page(w->plat->row[y]);
    kfree(w->plat); w->plat = 0;
}
static void open_platter(const char *path) {
    struct platter *p = kmalloc(sizeof(struct platter));
    if (!p) return;
    memset(p, 0, sizeof(*p));
    const char *why = bmp_read(path, p->row, &p->w, &p->h);
    if (why) {
        kfree(p);
        klog("[countertop] platter %s: %s\n", path, why);
        char m[48] = "platter: "; scopy(m + 9, why, (int)sizeof(m) - 9);
        bar_note(m); note_showing = 1; note_at = timer_get_ticks();
        return;
    }
    const char *name = path;
    for (const char *c = path; *c; c++) if (*c == '/') name = c + 1;
    int bw = p->w < W - 80 ? p->w : W - 80, bh = p->h < H - BAR_H - TASK_H - 80 ? p->h : H - BAR_H - TASK_H - 80;
    if (bw < 160) bw = 160;                            /* room for the title and its boxes */
    int n = 0;
    for (int i = 0; i < MAX_WIN; i++) if (win[i].used && win[i].plat) n++;
    static const char *const none[] = { 0 };
    open_hidden = 1;                                   /* drawn once it is a platter */
    int k = open_window(40 + 28 * n, BAR_H + 16 + 28 * n, bw + 2 * BORDER, bh + 2 * BORDER + TITLE_H, name, 0, none);
    open_hidden = 0;
    if (k < 0) { for (int y = 0; y < p->h; y++) pmm_free_page(p->row[y]); kfree(p); return; }
    win[k].plat = p; win[k].hidden = win[k].min = 0;
    int was = -1;
    for (int i = nwin - 2; i >= 0; i--) if (!win[order[i]].min) { was = order[i]; break; }
    if (was >= 0) repaint_win(was);
    repaint_win(k);
    klog("[countertop] platter %s: %dx%d\n", path, p->w, p->h);
}
static void files_open(int k, int r) {
    struct files *f = win[k].files;
    if (r < 0 || r >= f->n) return;
    char p[FAT_PATH_MAX];
    if (files_path(f, r, p) < 0) return;
    if (f->dir[r]) {
        strcpy(f->path, p);
        files_load(f);
        klog("[countertop] files in %s, %d entries\n", f->path, f->n);
        repaint_win(k);
        return;
    }
    if (is_bmp(p)) { open_platter(p); return; }       /* a picture to a platter (v0.60.186) */
    int t = open_terminal(0);
    if (t < 0) return;
    const char *verb = is_text(p) ? "jot" : "pour";   /* text to the editor (v0.60.154) */
    for (const char *c = verb; *c; c++) wt_key(win[t].term, *c);
    wt_key(win[t].term, ' ');
    type_path(win[t].term, p);
    wt_key(win[t].term, '\n');
    klog("[countertop] %s %s\n", verb, p);
}

/* Alt+Tab (v0.60.155): the focused window goes to the back, so the next one
 * down has the focus; a second press reaches the third. Minimised windows
 * are skipped because the focus skips them. Alt was read when the Tab
 * arrived here, measured held for QEMU's alt-tab; since v0.60.171 the
 * keyboard IRQ decides it (KEY_ALT_TAB), since under load an Alt+F4 read
 * that way was lost. */
static void lower_window(int k) {
    int at = -1;
    for (int i = 0; i < nwin; i++) if (order[i] == k) at = i;
    if (at <= 0) return;
    for (int i = at; i > 0; i--) order[i] = order[i - 1];
    order[0] = k;
    repaint_win(k);                                   /* what now covers it */
    int t = topmost();
    if (t >= 0) { repaint_win(t); klog("[countertop] alt-tab to %s\n", win[t].title); }
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
/* Sessions whose windows have gone, kept until their tasks are done. */
static struct { wterm_t *term; void *sess; } dying[MAX_WIN];
static void reap_dying(void) {
    for (int i = 0; i < MAX_WIN; i++)
        if (dying[i].sess && shell_session_done(dying[i].sess, 0)) {
            shell_session_free(dying[i].sess);
            if (dying[i].term->sb) kfree(dying[i].term->sb);
            kfree(dying[i].term);
            dying[i].sess = 0; dying[i].term = 0;
        }
}
static void close_window(int k);
static void bar_note(const char *msg);
static void flush(void);
static int note_showing;                         /* with the notes, below */
static uint32_t note_at;
/* Closing the kitchen (v0.60.183): power off or restart from the desktop,
 * which is where the machine now starts. The headchef's only, as freeze
 * and reheat are at the prompt. Every terminal is hung up first, and its
 * session given up to 2 s to end, which saves its leftovers. */
static void kitchen_close(int reboot) {
    const char *what = reboot ? "reheat the kitchen" : "close the kitchen";
    if (ct_uid != 0) {
        char m[48] = "only the headchef may ";
        strcat(m, what);
        bar_note(m);
        note_showing = 1; note_at = timer_get_ticks();   /* goes as a note goes */
        klog("[countertop] %s may not %s\n", users_name_of((uint8_t)ct_uid), what);
        return;
    }
    klog("[countertop] %s\n", reboot ? "reheating the kitchen" : "closing the kitchen");
    for (int k = 0; k < MAX_WIN; k++) if (win[k].used && win[k].term) close_window(k);
    for (int t = 0; t < 200; t++) {                   /* the sessions end, leftovers saved */
        int left = 0;
        reap_dying();
        for (int i = 0; i < MAX_WIN; i++) if (dying[i].sess) left++;
        if (!left) break;
        task_sleep(10);
    }
    bar_note(reboot ? "reheating..." : "the kitchen is closed");
    flush();
    if (reboot) power_reboot(); else power_off();
}
static void close_window(int k) {
    if (ctx_win == k) ctx_close();
    if (win[k].files) { kfree(win[k].files); win[k].files = 0; }
    plat_free(&win[k]);
    if (win[k].app >= 0) {                             /* its program ended, or closed it */
        mutex_lock(&app_mtx);
        app_free(&app[win[k].app]);
        mutex_unlock(&app_mtx);
        win[k].app = -1;
        if (app_drag == k) app_drag = -1;
    }
    if (win[k].term) {
        win[k].term->closed = 1;                       /* its shell's getc gives -1 */
        shell_session_hangup(win[k].sess);             /* its programs die with it */
        for (int i = 0; i < MAX_WIN; i++) if (!dying[i].sess) { dying[i].sess = win[k].sess; dying[i].term = win[k].term; break; }
        win[k].term = 0; win[k].sess = 0;
    }
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
/* Once a loop: show new program windows, repaint changed ones, close
 * those whose program closed them or has ended. */
static void flush(void);
static void apps_tick(void) {
    mutex_lock(&app_mtx);
    for (int i = 0; i < APP_MAX; i++) {
        app_t *a = &app[i];
        if (!a->used) continue;
        proc_t *p = proc_by_pid(a->pid);
        int ended = !p || p->state == PROC_ZOMBIE;
        if (a->win >= 0 && (ended || a->gone == 2)) {
            klog("[countertop] %s %s\n", a->gone == 2 ? "program closed" : "program ended, close", a->title);
            close_window(a->win);                      /* frees the slot */
            continue;
        }
        if (a->win < 0 && (ended || a->gone == 2)) { app_free(a); continue; }
        if (a->win < 0 && !a->gone) {
            int n = 0;
            for (int j = 0; j < APP_MAX; j++) if (j != i && app[j].used && app[j].win >= 0) n++;
            int ww = a->w + 2 * BORDER, wh = a->h + 2 * BORDER + TITLE_H;
            int x = 240 + n * 28, y = 70 + n * 28;
            if (x + ww > W) x = W - ww;
            if (y + wh > H - TASK_H) y = H - TASK_H - wh;
            if (x < 0) x = 0;
            if (y < BAR_H) y = BAR_H;
            static const char *const none[] = { 0 };
            int k = open_window(x, y, ww, wh, a->title, 0, none);
            if (k < 0) continue;                       /* no room yet: next time */
            win[k].app = i; a->win = k; a->dirty = 0;
            klog("[countertop] open %s for pid %u at %d,%d\n", a->title, a->pid, x, y);
            repaint_win(k);
            continue;
        }
        if (a->win >= 0 && a->dirty) {                 /* drawn, and its share of the copy out */
            uint64_t t = rdtsc_();
            a->dirty = 0; repaint_win(a->win); flush();
            a->draw_tsc += rdtsc_() - t; a->drawn++;
        }
    }
    mutex_unlock(&app_mtx);
}

static int window_at(int x, int y) {
    for (int i = nwin - 1; i >= 0; i--) {
        win_t *w = &win[order[i]];
        if (w->min) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return order[i];
    }
    return -1;
}

/* The mouse's buttons, once a tick: a press picks a window, a drag moves
 * it, the close box closes it. */
/* ---- the taskbar (v0.60.144) ------------------------------------------- */

static void taskbar(void) {
    int y = H - TASK_H;
    fill(0, y, W, TASK_H, BAR_RGB);
    fill(8, y + 4, LAUNCH_W, TASK_H - 8, BTN_RGB);
    text(16, y + (TASK_H - 16) / 2, "Terminal", TEXT_RGB);
    int x = 8 + LAUNCH_W + 12, top = topmost();
    for (int i = 0; i < MAX_WIN; i++) {             /* in the order they were opened */
        if (!win[i].used || win[i].hidden) continue;
        if (x + TBTN_W > W - 8) break;
        uint32_t c = i == top ? FOCUS_RGB : win[i].min ? 0x002A2E34u : BTN_RGB;
        fill(x, y + 4, TBTN_W, TASK_H - 8, c);
        char t[18]; int n = 0;
        for (; win[i].title[n] && n < 17; n++) t[n] = win[i].title[n];
        t[n] = '\0';
        text(x + 8, y + (TASK_H - 16) / 2, t, win[i].min ? DIM_RGB : TEXT_RGB);
        x += TBTN_W + 6;
    }
}
static int taskbar_hit(int px, int py) {           /* the window under a taskbar button, -2 the launcher */
    int y = H - TASK_H;
    if (py < y + 4 || py >= H - 4) return -1;
    if (px >= 8 && px < 8 + LAUNCH_W) return -2;
    int x = 8 + LAUNCH_W + 12;
    for (int i = 0; i < MAX_WIN; i++) {
        if (!win[i].used || win[i].hidden) continue;
        if (px >= x && px < x + TBTN_W) return i;
        x += TBTN_W + 6;
    }
    return -1;
}
static void minimise_window(int k) {
    int was = topmost();
    win[k].min = 1;
    klog("[countertop] minimise %s\n", win[k].title);
    repaint_win(k);
    if (topmost() >= 0 && topmost() != was) repaint_win(topmost());
}
static void restore_window(int k) {
    win[k].min = 0;
    klog("[countertop] restore %s\n", win[k].title);
    int at = -1, was = -1;
    for (int i = 0; i < nwin; i++) if (order[i] == k) at = i;
    for (int i = nwin - 1; i >= 0; i--) if (order[i] != k && !win[order[i]].min) { was = order[i]; break; }
    for (int i = at; i < nwin - 1; i++) order[i] = order[i + 1];
    order[nwin - 1] = k;
    if (was >= 0) repaint_win(was);
    repaint_win(k);
}

static int leave_asked;
/* Leave with programs' windows open (v0.60.165): the first Leave (its button
 * or Esc) says how many on the bar and arms it; a second within 5 s
 * leaves. With none open it leaves at once. */
#define NOTE_X (BTN_KITCHEN_X + BTN_KITCHEN_W + 16)
#define NET_X (W - 8 * 8 - 12 - 12 - 16 * 8)       /* the network's 16 characters, before the clock */
static int leave_armed;
static uint32_t leave_armed_at;
static int apps_open(void) {
    int n = 0;
    mutex_lock(&app_mtx);
    for (int i = 0; i < APP_MAX; i++) if (app[i].used && app[i].win >= 0 && !app[i].gone) n++;
    mutex_unlock(&app_mtx);
    return n;
}
static void bar_note(const char *msg) {          /* msg 0 clears it */
    fill(NOTE_X, 0, NET_X - 8 - NOTE_X, BAR_H, BAR_RGB);   /* up to the network (v0.60.181) */
    if (msg) {
        char m[48];                                    /* no further than the network */
        scopy(m, msg, (NET_X - 8 - NOTE_X) / 8 + 1 < (int)sizeof(m) ? (NET_X - 8 - NOTE_X) / 8 + 1 : (int)sizeof(m));
        text(NOTE_X, (BAR_H - 16) / 2, m, 0x00F0B040u);
    }
}
/* Notes from programs (v0.60.170): queued here by SYS_NOTE (on the
 * program's task, under app_mtx), shown on the bar a few seconds each by
 * the loop. Leave's question has the bar first. */
#define NOTE_Q 8
#define NOTE_TICKS 400
static char note_q[NOTE_Q][48];
static int note_h, note_t, note_showing;
static uint32_t note_at;
int countertop_note(uint32_t pid, const char *line) {
    mutex_lock(&app_mtx);
    int r = -1;
    if (ct_running && (note_t + 1) % NOTE_Q != note_h) {
        scopy(note_q[note_t], line, (int)sizeof(note_q[0]));
        for (char *c = note_q[note_t]; *c; c++) if ((unsigned char)*c < ' ' || (unsigned char)*c > 126) *c = ' ';
        note_t = (note_t + 1) % NOTE_Q;
        r = 0;
    }
    mutex_unlock(&app_mtx);
    if (!r) klog("[countertop] note from pid %u queued\n", pid);
    return r;
}
static void notes_tick(void) {
    if (leave_armed) return;
    if (note_showing && timer_get_ticks() - note_at >= NOTE_TICKS) { note_showing = 0; bar_note(0); }
    if (note_showing) return;
    char line[48];
    mutex_lock(&app_mtx);
    int have = note_h != note_t;
    if (have) { scopy(line, note_q[note_h], (int)sizeof(line)); note_h = (note_h + 1) % NOTE_Q; }
    mutex_unlock(&app_mtx);
    if (!have) return;
    bar_note(line);
    note_showing = 1; note_at = timer_get_ticks();
    klog("[countertop] note: %s\n", line);
}
static void ask_leave(void) {
    int n = apps_open();
    if (!n || (leave_armed && timer_get_ticks() - leave_armed_at < 500)) { leave_asked = 1; return; }
    leave_armed = 1; leave_armed_at = timer_get_ticks();
    note_showing = 0;                                  /* the question has the bar now */
    char msg[64] = "programs open: Leave again to end them";
    if (n == 1) scopy(msg, "a program is open: Leave again to end it", (int)sizeof(msg));
    bar_note(msg);
    klog("[countertop] leave: %d program window%s open, asking again\n", n, n == 1 ? "" : "s");
}
static uint32_t task_sig;                         /* the windows as the taskbar last showed them */
/* Maximise and restore (v0.60.161): a double click on a title bar fills
 * the space between the bars with the window, and another puts it back. */
static void maximise_window(int k) {
    win_t *w = &win[k];
    int ox = w->x, oy = w->y, ow = w->w, oh = w->h;
    if (!w->maxed) {
        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
        w->x = 0; w->y = BAR_H; w->w = W; w->h = H - BAR_H - TASK_H; w->maxed = 1;
    } else {
        w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh; w->maxed = 0;
    }
    if (w->files) {                                    /* its rows fit the new height */
        int rows = files_rows(w);
        if (w->files->sel >= w->files->top + rows) w->files->top = w->files->sel - rows + 1;
        if (w->files->top > 0 && w->files->top + rows > w->files->n) w->files->top = w->files->n > rows ? w->files->n - rows : 0;
    }
    repaint(ox, oy, ow, oh);
    repaint_win(k);
    klog("[countertop] %s %s\n", w->maxed ? "maximise" : "restore", w->title);
}

/* Snap to halves (v0.60.167): dragged by its title bar to the left or right
 * edge, a window fills that half between the bars; dragged off again it is
 * its own size. A terminal's cells follow (80 columns do not fit 512
 * pixels) and come back after. side 0 unsnaps. */
static void snap_window(int k, int side) {
    win_t *w = &win[k];
    int ox = w->x, oy = w->y, ow = w->w, oh = w->h;
    if (side && !w->snapped) {
        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
        if (w->term) { w->rcols = w->term->cols; w->rrows = w->term->rows; }
    }
    if (side) {
        w->x = side == 1 ? 0 : W / 2; w->y = BAR_H; w->w = W / 2; w->h = H - BAR_H - TASK_H;
        if (w->term) {
            int c = (w->w - 2 * BORDER) / 8, r = (w->h - 2 * BORDER - TITLE_H) / 16;
            wt_resize(w->term, c > WMAXC ? WMAXC : c, r > WMAXR ? WMAXR : r);
        }
    } else {
        w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
        if (w->term) wt_resize(w->term, w->rcols, w->rrows);
    }
    w->snapped = side;
    if (w->files) {
        int rows = files_rows(w);
        if (w->files->sel >= w->files->top + rows) w->files->top = w->files->sel - rows + 1;
    }
    repaint(ox, oy, ow, oh);
    repaint_win(k);
    klog("[countertop] %s %s\n", side == 1 ? "snap left" : side == 2 ? "snap right" : "unsnap", w->title);
}

/* Copy and paste in terminal windows (v0.60.160). A left drag over a
 * terminal's cells selects them in reading order; letting go copies them
 * (each line's trailing blanks dropped, lines joined by newlines) to the
 * cook's clipboard, the one jot and the shell's Ctrl+U share. A right or
 * middle click types the clipboard into the terminal under it. A click
 * with no drag, or new output, clears the selection. */
static int sel_win = -1;
/* A row of a files window dragged (v0.60.163): let go over a terminal, its
 * path is typed there, quoted, with no Enter; over another files window
 * (v0.60.177), the file is copied into the bowl that one shows; let go
 * anywhere else, or hardly moved, nothing happens. */
static int fd_win = -1, fd_row, fd_x, fd_y;
static int rs_win = -1, rs_dx, rs_dy;            /* a terminal being resized from its corner (v0.60.162) */
#define GRIP 12
static int cell_at(const win_t *w, int px, int py) {
    int bx, by;
    body_origin(w, &bx, &by);
    int c = (px - bx) / 8, r = (py - by) / 16;
    if (px < bx) c = -1;
    if (py < by) r = -1;
    if (c < 0) c = 0;
    if (c >= w->term->cols) c = w->term->cols - 1;
    if (r < 0) r = 0;
    if (r >= w->term->rows) r = w->term->rows - 1;
    return r * w->term->cols + c;
}
static void sel_copy(int k) {
    wterm_t *t = win[k].term;
    int lo = t->sel_a < t->sel_b ? t->sel_a : t->sel_b, hi = t->sel_a ^ t->sel_b ^ lo;
    static char buf[WMAXR * (WMAXC + 1)];
    int C = t->cols;
    int n = 0;
    for (int r = lo / C; r <= hi / C; r++) {
        int c0 = r == lo / C ? lo % C : 0, c1 = r == hi / C ? hi % C : C - 1;
        int start = n;
        for (int c = c0; c <= c1; c++) buf[n++] = (char)(t->cell[r * C + c] & 0xFF);
        while (n > start && buf[n - 1] == ' ') n--;          /* trailing blanks */
        if (r < hi / C) buf[n++] = '\n';
    }
    clip_set(buf, (uint32_t)n);
    klog("[countertop] copied %d bytes from %s\n", n, win[k].title);
}

/* np: the left presses since the last look (the IRQ counts them, so a click
 * shorter than the loop's nap still counts, v0.60.145). */
static void pointer_buttons(int px, int py, int btn, int np) {
    int left = btn & 1, was = last_btn;
    last_btn = btn;
    last_input = timer_get_ticks();
    if (locked) return;                                /* under the cloche: no clicks */
    if (fd_win >= 0 && (!left || np)) {                /* a files row let go */
        int k = window_at(px, py), from = fd_win;
        fd_win = -1;
        int moved = (px - fd_x) * (px - fd_x) + (py - fd_y) * (py - fd_y) > 64;
        if (moved && k >= 0 && k != from && win[k].files && win[from].files) files_copy(from, fd_row, k);
        else if (moved && k >= 0 && k != from && win[k].term && win[from].files) {
            char p[FAT_PATH_MAX];
            if (files_path(win[from].files, fd_row, p) == 0) {
                raise_window(k);
                type_path(win[k].term, p);
                klog("[countertop] dropped %s on %s\n", p, win[k].title);
            }
        }
        if (!np) return;
    }
    if (rs_win >= 0) {                                 /* a corner being dragged: whole cells */
        win_t *w = &win[rs_win];
        if (w->term) {
            int maxc = (W - w->x - 2 * BORDER) / 8, maxr = (H - TASK_H - w->y - 2 * BORDER - TITLE_H) / 16;
            if (maxc > WMAXC) maxc = WMAXC;
            if (maxr > WMAXR) maxr = WMAXR;
            int nc = (px + rs_dx - w->x - 2 * BORDER) / 8, nr = (py + rs_dy - w->y - 2 * BORDER - TITLE_H) / 16;
            if (nc > maxc) nc = maxc;
            if (nr > maxr) nr = maxr;
            if (nc < 40) nc = 40;
            if (nr < 10) nr = 10;
            if (nc != w->term->cols || nr != w->term->rows) {
                int ox = w->x, oy = w->y, ow = w->w, oh = w->h;
                wt_resize(w->term, nc, nr);
                w->w = nc * 8 + 2 * BORDER; w->h = nr * 16 + 2 * BORDER + TITLE_H;
                repaint(ox, oy, ow, oh);
                repaint_win(rs_win);
            }
            if (!left) klog("[countertop] resize %s to %dx%d\n", w->title, w->term->cols, w->term->rows);
        }
        if (!left || !w->term) rs_win = -1;
        if (!np) return;
    }
    if (sel_win >= 0) {                                /* a selection being dragged */
        win_t *w = &win[sel_win];
        if (w->term) {
            int i = cell_at(w, px, py);
            if (i != w->term->sel_b) { w->term->sel_b = i; repaint_win(sel_win); }
            if (!left) {
                if (w->term->sel_a != w->term->sel_b) sel_copy(sel_win);
                else { w->term->sel_a = w->term->sel_b = -1; repaint_win(sel_win); }
            }
        }
        if (!left || !w->term) sel_win = -1;
        if (!np) return;
    }
    if ((btn & 6) && !(was & 6)) {                     /* right or middle: paste into a terminal */
        int k = window_at(px, py);
        if ((btn & 2) && k < 0 && py >= BAR_H && py < H - TASK_H && icon_at(px, py) < 0) {   /* the background: its menu */
            menu_set(0);
            desk_set(1, px, py);
            return;
        }
        if (k >= 0 && win[k].files && py >= win[k].y + BORDER + TITLE_H) {   /* a files window's menu (v0.60.172) */
            struct files *f = win[k].files;
            int y0 = win[k].y + BORDER + TITLE_H + 28, r = -1;
            if (py >= y0) r = f->top + (py - y0) / FROW_H;
            if (r >= f->n || (r >= 0 && !strcmp(f->name[r], ".."))) r = -1;
            raise_window(k);
            ctx_open(k, r, px, py);
            return;
        }
        if (k >= 0 && win[k].term && py >= win[k].y + BORDER + TITLE_H) {
            raise_window(k);
            uint32_t n = clip_len();
            const char *d = clip_peek();
            for (uint32_t i = 0; i < n; i++) wt_key(win[k].term, d[i]);
            klog("[countertop] pasted %u bytes into %s\n", n, win[k].title);
            return;
        }
    }
    if (app_drag >= 0) {                               /* a program's body, the button held */
        win_t *w = &win[app_drag];
        int bx, by;
        body_origin(w, &bx, &by);
        int x = px - bx, y = py - by;
        if (x != app_lx || y != app_ly) { app_push(w->app, WEV_MOVE, 0, x, y, (unsigned)btn); app_lx = x; app_ly = y; }
        if (!left || np) { app_push(w->app, WEV_UP, 0, x, y, (unsigned)btn); app_drag = -1; }
        if (!np) return;
    }
    if (np && ctx_win >= 0 && ctx_click(px, py)) return;   /* the files menu (v0.60.172) */
    if (np && desk_open) {                             /* the background's menu (v0.60.184) */
        int in = px >= desk_x && px < desk_x + DESK_W && py >= desk_y + 2 && py < desk_y + 2 + DESK_N * DESK_IH;
        int i = (py - desk_y - 2) / DESK_IH;
        desk_set(0, 0, 0);
        if (in) {
            klog("[countertop] background menu: %s\n", desk_item[i]);
            if (i == 0) open_terminal(0);
            else if (i == 1) open_files("/");
            else if (i == 2) menu_set(1);
            else if (i == 3) next_background();
            else cloche_lock("the background menu");
            return;
        }
    }
    if (np && py < BAR_H && px >= W - 8 * 8 - 12) { cal_set(!cal_open); return; }   /* the clock: the calendar */
    if (np && cal_open) cal_set(0);                    /* a click anywhere else puts it up, and goes on */
    if (np && menu_open) {                             /* a click with the menu down: an item, or away */
        int i = (py - MENU_Y - 2) / MENU_IH;
        int on_button = py < BAR_H && px >= BTN_KITCHEN_X && px < BTN_KITCHEN_X + BTN_KITCHEN_W;
        menu_set(0);
        if (px >= MENU_X && px < MENU_X + MENU_W && py >= MENU_Y + 2 && i >= 0 && i < MENU_N && menu_item[i][2][0] == '@') {
            if (!strcmp(menu_item[i][2], "@cloche")) cloche_lock("the menu");
            else if (!strcmp(menu_item[i][2], "@close")) kitchen_close(0);
            else if (!strcmp(menu_item[i][2], "@reheat")) kitchen_close(1);
            else next_background();
        } else if (px >= MENU_X && px < MENU_X + MENU_W && py >= MENU_Y + 2 && i >= 0 && i < MENU_N) {
            kitchen_launch(i);
        }
        if (on_button || (px >= MENU_X && px < MENU_X + MENU_W && py < MENU_Y + MENU_H)) return;
    }
    if (np && py < BAR_H) {
        if (px >= BTN_KITCHEN_X && px < BTN_KITCHEN_X + BTN_KITCHEN_W) { menu_set(1); return; }
        if (px >= BTN_TERM_X && px < BTN_TERM_X + BTN_TERM_W) open_terminal(0);
        else if (px >= BTN_LEAVE_X && px < BTN_LEAVE_X + BTN_LEAVE_W) ask_leave();
        else if (px >= BTN_FILES_X && px < BTN_FILES_X + BTN_FILES_W) open_files("/");
        return;
    }
    if (np && py >= H - TASK_H) {
        int k = taskbar_hit(px, py);
        if (k == -2) open_terminal(0);
        else if (k >= 0 && win[k].min) restore_window(k);
        else if (k >= 0 && k == topmost()) minimise_window(k);
        else if (k >= 0) raise_window(k);
        return;
    }
    if (np) {
        int k = window_at(px, py);
        if (k < 0) {                                   /* the background: an icon, twice quickly, opens */
            static int last_icon = -1;
            int ic = icon_at(px, py);
            uint32_t now, before;
            mouse_presses(&now, &before);
            if (ic >= 0 && (np >= 2 || (last_icon == ic && now - before < DCLICK))) { last_icon = -1; kitchen_launch(ic); }
            else last_icon = ic;
            return;
        }
        win_t *w = &win[k];
        int bx = w->x + w->w - BORDER - 3 - CLOSE_SZ, by = w->y + BORDER + (TITLE_H - CLOSE_SZ) / 2;
        if (px >= bx && px < bx + CLOSE_SZ && py >= by && py < by + CLOSE_SZ) {
            if (w->app >= 0) { app_push(w->app, WEV_CLOSE, 0, 0, 0, 0); klog("[countertop] close asked of %s\n", w->title); }
            else close_window(k);                      /* a program's window is the program's to close */
            return;
        }
        if (px >= bx - 4 - CLOSE_SZ && px < bx - 4 && py >= by && py < by + CLOSE_SZ) { minimise_window(k); return; }
        raise_window(k);
        if (w->term && !w->maxed && px >= w->x + w->w - GRIP && py >= w->y + w->h - GRIP) {   /* its corner: resize */
            rs_win = k; rs_dx = w->x + w->w - px; rs_dy = w->y + w->h - py;
            return;
        }
        if (w->term && w->term->back && py >= w->y + BORDER + TITLE_H) return;   /* scrolled back: no selection (it is of live cells) */
        if (w->term && py >= w->y + BORDER + TITLE_H) {     /* a terminal's cells: a selection starts */
            int had = w->term->sel_a >= 0;
            w->term->sel_a = w->term->sel_b = cell_at(w, px, py);
            sel_win = k;
            if (had) repaint_win(k);
            return;
        }
        if (w->app >= 0 && py >= w->y + BORDER + TITLE_H) {      /* the body: the program's */
            int ax, ay;
            body_origin(w, &ax, &ay);
            app_drag = k; app_lx = px - ax; app_ly = py - ay;
            app_push(w->app, WEV_DOWN, 0, app_lx, app_ly, (unsigned)(btn | 1));
            return;
        }
        if (w->files && py >= w->y + BORDER + TITLE_H + 28) {   /* a row: select, twice quickly: open */
            static int last_k = -1, last_r = -1;
            int r = w->files->top + (py - (w->y + BORDER + TITLE_H + 28)) / FROW_H;
            if (r < w->files->n) {
                uint32_t now, before;
                mouse_presses(&now, &before);               /* timed in the IRQ */
                int twice = np >= 2 || (last_k == k && last_r == r && now - before < DCLICK);
                w->files->sel = r; repaint_win(k);
                last_k = k; last_r = r;
                if (twice) { last_k = -1; files_open(k, r); }
                else if (strcmp(w->files->name[r], "..")) { fd_win = k; fd_row = r; fd_x = px; fd_y = py; }   /* maybe a drag */
            }
            return;
        }
        if (py < w->y + BORDER + TITLE_H) {               /* the title bar: drag, or twice: maximise */
            static int last_tk = -1;
            uint32_t now, before;
            mouse_presses(&now, &before);
            if (np >= 2 || (last_tk == k && now - before < DCLICK)) { last_tk = -1; maximise_window(k); return; }
            last_tk = k;
            if (!w->maxed) { drag = k; drag_dx = px - w->x; drag_dy = py - w->y; drag_ox = w->x; drag_oy = w->y; }
        }
        return;
    }
    if (left && drag >= 0) {
        win_t *w = &win[drag];
        int edge = px <= 0 ? 1 : px >= W - 1 ? 2 : 0;
        if (edge && w->snapped != edge) { snap_window(drag, edge); return; }
        if (w->snapped && (edge || px < 20 || px > W - 21)) return;   /* held at the edge */
        if (w->snapped) {                                  /* off the edge again: its own size, under the pointer */
            snap_window(drag, 0);
            int ox = w->x, oy = w->y;
            w->x = px - w->w / 2; w->y = py - TITLE_H / 2;
            if (w->y < BAR_H) w->y = BAR_H;
            drag_dx = px - w->x; drag_dy = py - w->y;
            repaint(ox, oy, w->w, w->h);                   /* where snap_window just put it back */
            repaint_win(drag);
        }
        int nx = px - drag_dx, ny = py - drag_dy;
        if (ny < BAR_H) ny = BAR_H;                       /* not under the bar */
        if (ny > H - TASK_H - TITLE_H) ny = H - TASK_H - TITLE_H;   /* a title bar left to grab, above the taskbar */
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
    local_now(&t);
    if ((int)t.second == clock_sec) return;
    clock_sec = (int)t.second;
    char s[9] = { (char)('0' + t.hour / 10), (char)('0' + t.hour % 10), ':', (char)('0' + t.minute / 10),
                  (char)('0' + t.minute % 10), ':', (char)('0' + t.second / 10), (char)('0' + t.second % 10), 0 };
    if (clock12) {
        int h = t.hour % 12 ? t.hour % 12 : 12;
        s[0] = h >= 10 ? '1' : ' '; s[1] = (char)('0' + h % 10); s[2] = ':';
        s[3] = (char)('0' + t.minute / 10); s[4] = (char)('0' + t.minute % 10);
        s[5] = ' '; s[6] = t.hour < 12 ? 'a' : 'p'; s[7] = 'm';
    }
    fill(W - 8 * 8 - 12, 0, 8 * 8 + 12, BAR_H, BAR_RGB);
    text(W - 8 * 8 - 8, (BAR_H - 16) / 2, s, TEXT_RGB);
}

static void button(int x, int w, const char *label) {
    fill(x, 3, w, BAR_H - 6, BTN_RGB);
    text(x + 8, (BAR_H - 16) / 2, label, TEXT_RGB);
}
/* The network on the bar (v0.60.181), right-aligned against the clock:
 * "no network" with no card; the address with one, dim while it is the
 * compiled-in one and white once DHCP has leased it. Looked at every 3 s;
 * drawn when it changes. Notes and Leave's question end before it. */
static char net_shown[20];
static int net_lit;
static void bar_net(int force) {
    char s[20];
    int lit = 0;
    if (!rtl8139_present()) scopy(s, "no network", (int)sizeof(s));
    else {
        uint32_t ip = net_ip();
        int l = 0;
        for (int sh = 24; sh >= 0; sh -= 8) {
            unsigned v = (ip >> sh) & 0xFF;
            if (v >= 100) s[l++] = (char)('0' + v / 100);
            if (v >= 10) s[l++] = (char)('0' + v / 10 % 10);
            s[l++] = (char)('0' + v % 10);
            if (sh) s[l++] = '.';
        }
        s[l] = '\0';
        lit = net_leased();
    }
    if (!force && lit == net_lit && !strcmp(s, net_shown)) return;
    int changed = lit != net_lit || strcmp(s, net_shown);
    scopy(net_shown, s, (int)sizeof(net_shown)); net_lit = lit;
    fill(NET_X, 0, 16 * 8, BAR_H, BAR_RGB);
    text(NET_X + 16 * 8 - 8 * (int)strlen(s), (BAR_H - 16) / 2, s, lit ? TEXT_RGB : DIM_RGB);
    if (changed) klog("[countertop] network: %s%s\n", s, rtl8139_present() && !lit ? " (not leased)" : "");
}
static void bar(void) {
    fill(0, 0, W, BAR_H, BAR_RGB);
    text(10, (BAR_H - 16) / 2, "soupOS", TEXT_RGB);
    text(10 + 8 * 7, (BAR_H - 16) / 2, "countertop", DIM_RGB);
    button(BTN_TERM_X, BTN_TERM_W, "Terminal");
    button(BTN_LEAVE_X, BTN_LEAVE_W, "Leave");
    button(BTN_FILES_X, BTN_FILES_W, "Files");
    button(BTN_KITCHEN_X, BTN_KITCHEN_W, "Kitchen");
    bar_net(1);
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

/* A picture of the screen (v0.60.185): Print Screen writes the back buffer
 * as a 24-bit BMP, bottom-up, into the cook's home as plateN.bmp (the first
 * N not taken), streamed through fat_write_from. The pointer is not in it:
 * it is put on only on the way to the framebuffer. Nothing draws into the
 * back buffer while this runs, since only this loop does. */
static int plate_pull(void *ctx, uint32_t off, uint8_t *dst, uint32_t n) {
    (void)ctx;
    uint32_t stride = ((uint32_t)W * 3 + 3) & ~3u;
    for (uint32_t i = 0; i < n; i++, off++) {
        if (off < 54) {
            static uint8_t hd[54];
            if (off == 0) {
                uint32_t img = stride * (uint32_t)H, v[13] = { 54 + img, 0, 54, 40, (uint32_t)W, (uint32_t)H, 0, 0, img, 2835, 2835, 0, 0 };
                memset(hd, 0, sizeof(hd));
                hd[0] = 'B'; hd[1] = 'M';
                for (int k = 0; k < 3; k++) for (int b = 0; b < 4; b++) hd[2 + k * 4 + b] = (uint8_t)(v[k] >> (8 * b));
                for (int k = 3; k < 6; k++) for (int b = 0; b < 4; b++) hd[14 + (k - 3) * 4 + b] = (uint8_t)(v[k] >> (8 * b));
                hd[26] = 1; hd[28] = 24;                   /* planes, bits */
                for (int k = 8; k < 13; k++) for (int b = 0; b < 4; b++) hd[34 + (k - 8) * 4 + b] = (uint8_t)(v[k] >> (8 * b));
            }
            dst[i] = hd[off];
            continue;
        }
        uint32_t p = off - 54, r = p / stride, b = p % stride;
        if (b >= (uint32_t)W * 3) { dst[i] = 0; continue; }   /* row padding */
        uint32_t px = row[H - 1 - (int)r][b / 3];
        dst[i] = (uint8_t)(px >> (8 * (b % 3)));          /* blue, green, red */
    }
    return 0;
}
static void plate(void) {
    char home[FAT_PATH_MAX], path[FAT_PATH_MAX];
    if (ct_uid == 0) strcpy(home, "/");
    else { strcpy(home, "/home/"); strcat(home, users_name_of((uint8_t)ct_uid)); }   /* names are 16 at most */
    if (!users_may(home, 'w')) { klog("[countertop] plate: may not write %s\n", home); bar_note("plate: your home is not yours to write"); note_showing = 1; note_at = timer_get_ticks(); return; }
    int n = 1;
    for (; n < 1000; n++) {
        strcpy(path, home);
        if (strcmp(home, "/")) strcat(path, "/");
        strcat(path, "plate");
        char d[5]; int k = 0, v = n;
        do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        int l = (int)strlen(path);
        while (k) path[l++] = d[--k];
        path[l] = '\0';
        strcat(path, ".bmp");
        if (!fat_exists(path)) break;
    }
    uint32_t t0 = timer_get_ticks(), size = 54 + (((uint32_t)W * 3 + 3) & ~3u) * (uint32_t)H;
    int ok = n < 1000 && fat_write_from(path, size, plate_pull, 0) == 0;
    klog("[countertop] plate %s %s, %u bytes in %u ms\n", ok ? "saved" : "not saved:", path, size, (timer_get_ticks() - t0) * 10);
    char m[48] = "plate: not saved";
    if (ok) { strcpy(m, "saved "); scopy(m + 6, path, (int)sizeof(m) - 6); }
    bar_note(m); note_showing = 1; note_at = timer_get_ticks();
}

static void release(int pages) {
    for (int i = 0; i < pages; i++) { pmm_free_page(row[i]); row[i] = 0; }
}

int countertop_run(int (*avail)(void), int (*getch)(void), int uid);
int countertop_clock_in(int (*avail)(void), int (*getch)(void), int (*check)(const char *, const char *)) {
    login_cb = check;
    int r = countertop_run(avail, getch, -1);
    login_mode = 0; login_cb = 0;
    return r;
}
int countertop_run(int (*avail)(void), int (*getch)(void), int uid) {
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
    ct_uid = uid;
    if (uid >= 0) load_settings();                   /* before the first background (v0.60.169) */
    else { bg_choice = 0; idle_s = 600; }            /* nobody's yet: clocking in first (v0.60.179) */
    background();
    draw_icons();
    net_shown[0] = '\0'; net_lit = -1;              /* said again for each desktop */
    bar();
    nwin = 0; drag = -1; last_btn = 0; leave_asked = 0; leave_armed = 0; note_h = note_t = note_showing = 0; menu_open = 0; rs_win = -1; sel_win = -1; fd_win = -1; ctx_win = -1; cal_open = 0; locked = 0; lock_wait = 0; desk_open = 0;
    last_input = timer_get_ticks(); ct_uid = uid; nterm = 0; task_sig = 0xFFFFFFFFu;
    for (int k = 0; k < MAX_WIN; k++) win[k].used = 0;
    app_drag = -1;
    mutex_lock(&app_mtx);                            /* programs may ask for windows now */
    for (int i = 0; i < APP_MAX; i++) {
        proc_t *p = proc_by_pid(app[i].pid);
        if (app[i].used && (!p || p->state == PROC_ZOMBIE)) app_free(&app[i]);   /* left from last time */
    }
    ct_running = 1;
    mutex_unlock(&app_mtx);
    static const char *const welcome[] = { "Welcome to countertop.", "", "Drag a window by its title bar,",
                                           "click one to bring it forward,", "and the red box closes it.", "",
                                           "Esc gives the console back.", 0 };
    static const char *const welcome_in[] = { "Welcome to countertop.", "", "Drag a window by its title bar,",
                                              "click one to bring it forward,", "and the red box closes it.", "",
                                              "Leave or Esc clocks out.", 0 };
    static const char *const pantry[] = { "The pantry.", "", "Windows of their own come next:",
                                          "terminals, files, programs.", 0 };
    open_window(120, 100, 420, 220, "Welcome", 0x00F4F1EAu, uid < 0 ? welcome_in : welcome);
    open_window(420, 260, 360, 240, "Pantry", 0x00E6EEF5u, pantry);
    int b;
    mouse_get_px(&ptr_x, &ptr_y, &b);
    last_btn = b;
    presses_seen = mouse_presses(0, 0);
    wheel_seen = mouse_wheel();
    flush();
    klog("[countertop] up %dx%d, %d pages for the back buffer\n", W, H, H);
    if (uid < 0) {                                   /* the clock-in screen over it all */
        cloche_lock("clock in");
        login_mode = 1; login_len = 0; lock_len = 0; lock_fails = 0;
        draw_lock();
        flush();
        klog("[countertop] clock in to start your shift\n");   /* as the console says it */
    }
    for (;;) {
        int px, py;
        mouse_get_px(&px, &py, &b);
        if (px != ptr_x || py != ptr_y) {
            last_input = timer_get_ticks();
            dirty(ptr_x, ptr_y, ARROW_W, ARROW_H);
            ptr_x = px; ptr_y = py;
            dirty(ptr_x, ptr_y, ARROW_W, ARROW_H);
        }
        uint32_t pc = mouse_presses(0, 0);
        int np = (int)(pc - presses_seen);
        presses_seen = pc;
        if (np || b != last_btn || drag >= 0 || app_drag >= 0 || sel_win >= 0 || rs_win >= 0 || fd_win >= 0) pointer_buttons(px, py, b, np);
        int wh = mouse_wheel(), dw = wh - wheel_seen;
        wheel_seen = wh;
        if (dw) {                                    /* the wheel: a files window under the pointer scrolls */
            int k = window_at(px, py);
            if (k >= 0 && win[k].app >= 0 && py >= win[k].y + BORDER + TITLE_H) {   /* a program's: its event (v0.60.157) */
                int bx, by;
                body_origin(&win[k], &bx, &by);
                app_push(win[k].app, WEV_WHEEL, (unsigned)dw, px - bx, py - by, 0);
            } else if (k >= 0 && win[k].term) {           /* a terminal: through its scrollback (v0.60.166) */
                wterm_t *t = win[k].term;
                int back = t->back - 3 * dw;
                if (back < 0) back = 0;
                if (back > t->sb_n) back = t->sb_n;
                if (back != t->back) {
                    t->back = back; t->sel_a = -1;
                    repaint_win(k);
                    klog("[countertop] %s scrolled back %d\n", win[k].title, back);
                }
            } else if (k >= 0 && win[k].plat) {           /* a platter: down the picture (v0.60.186) */
                struct platter *pl = win[k].plat;
                int most = pl->h - plat_view_h(&win[k]), top = pl->top + 48 * dw;
                if (top > most) top = most;
                if (top < 0) top = 0;
                if (top != pl->top) { pl->top = top; repaint_win(k); klog("[countertop] %s scrolled to %d\n", win[k].title, top); }
            } else if (k >= 0 && win[k].files) {
                struct files *f = win[k].files;
                int rows = files_rows(&win[k]), most = f->n > rows ? f->n - rows : 0;
                int top = f->top + 3 * dw;
                if (top > most) top = most;
                if (top < 0) top = 0;
                if (top != f->top) { f->top = top; repaint_win(k); klog("[countertop] files scrolled to row %d\n", top); }
            }
        }
        if (!locked) bar_clock();
        static uint32_t net_seen;
        if (!locked && timer_get_ticks() - net_seen >= 300) { net_seen = timer_get_ticks(); bar_net(0); }
        if (!locked && idle_s && ct_uid >= 0 && timer_get_ticks() - last_input >= idle_s * 100u) cloche_lock("idle");
        for (int i = 0; i < nwin; i++) {             /* terminals: drawn again, or gone */
            int k = order[i];
            if (!win[k].term) continue;
            if (shell_session_done(win[k].sess, 0)) { close_window(k); i = -1; continue; }
            if (win[k].term->changed) {
                win[k].term->changed = 0;
                if (k != sel_win) win[k].term->sel_a = -1;      /* new output: the selection goes */
                win[k].term->back = 0;                          /* and the view is the bottom again */
                repaint_win(k);
            }
        }
        reap_dying();
        apps_tick();
        while (avail()) {                            /* keys: the focused terminal's */
            int c = getch(), k = topmost();
            last_input = timer_get_ticks();
            if (locked) { cloche_key(c); continue; }   /* under the cloche: keys are for it alone */
            if (c == 27 && desk_open) { desk_set(0, 0, 0); continue; }   /* Esc puts the background's menu up first */
            if (c == KEY_PRINT) { plate(); continue; }       /* a picture of the screen (v0.60.185) */
            if (c == KEY_CTRL_ALT_T) {                         /* Ctrl+Alt+T: a terminal (v0.60.171) */
                klog("[countertop] ctrl-alt-t\n");
                open_terminal(0);
                continue;
            }
            if (c == KEY_ALT_F4 && k >= 0) {                  /* Alt+F4: as its close box (v0.60.171) */
                klog("[countertop] alt-f4 %s\n", win[k].title);
                if (win[k].app >= 0) app_push(win[k].app, WEV_CLOSE, 0, 0, 0, 0);
                else close_window(k);
                continue;
            }
            if (c == KEY_ALT_TAB && k >= 0) {                  /* Alt+Tab (v0.60.155; decided in the IRQ, v0.60.171) */
                lower_window(k);
                continue;
            }
            if (k >= 0 && win[k].term) {
                if (win[k].term->back) { win[k].term->back = 0; repaint_win(k); }   /* typing: back to the bottom */
                wt_key(win[k].term, c);
            }
            else if (k >= 0 && win[k].app >= 0) app_push(win[k].app, WEV_KEY, (unsigned)c, 0, 0, 0);
            else if (k >= 0 && win[k].files && (c == KEY_UP || c == KEY_DOWN || c == '\n')) {
                struct files *f = win[k].files;
                if (c == '\n') files_open(k, f->sel);
                else {
                    if (c == KEY_UP && f->sel > 0) f->sel--;
                    if (c == KEY_DOWN && f->sel < f->n - 1) f->sel++;
                    int rows = files_rows(&win[k]);
                    if (f->sel < f->top) f->top = f->sel;
                    if (f->sel >= f->top + rows) f->top = f->sel - rows + 1;
                    repaint_win(k);
                }
            }
            else if (c == 27) ask_leave();
        }
        if (leave_asked) break;
        if (leave_armed && timer_get_ticks() - leave_armed_at >= 500) { leave_armed = 0; bar_note(0); }   /* the question lapses */
        if (!locked) notes_tick();
        {                                            /* the taskbar, when the windows changed */
            uint32_t sig = (uint32_t)topmost() * 2654435761u;
            for (int i = 0; i < MAX_WIN; i++) sig = sig * 31u + (uint32_t)(win[i].used * 2 + win[i].min);
            if (sig != task_sig && !locked) { task_sig = sig; taskbar(); }
        }
        flush();
        task_sleep(15);
    }
    mutex_lock(&app_mtx);                            /* programs' windows: gone, their calls get -1 */
    ct_running = 0;
    for (int i = 0; i < APP_MAX; i++) if (app[i].used) {
        if (app[i].gone == 2) app_free(&app[i]);
        else { app[i].gone = 1; app[i].win = -1; }
    }
    mutex_unlock(&app_mtx);
    for (int i = 0; i < nwin; i++) {                 /* every session hung up, no window "closed" */
        win_t *w = &win[order[i]];
        if (!w->term) continue;
        w->term->closed = 1;
        shell_session_hangup(w->sess);
        for (int d = 0; d < MAX_WIN; d++) if (!dying[d].sess) { dying[d].sess = w->sess; dying[d].term = w->term; break; }
        w->term = 0; w->sess = 0;
    }
    nwin = 0;
    for (int tries = 0; tries < 300; tries++) {      /* and given 3 s to end */
        reap_dying();
        int left = 0;
        for (int i = 0; i < MAX_WIN; i++) if (dying[i].sess) left++;
        if (!left) break;
        task_sleep(10);
    }
    release(H);
    pic_free();
    fb_clear(0x00101828);                 /* the console's border, as vga13h leaves it */
    fbcon_resume();
    klog("[countertop] down\n");
    return 0;
}

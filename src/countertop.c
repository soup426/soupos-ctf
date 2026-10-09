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
static void background(void) {                  /* within the clip */
    int span = H - BAR_H - 1 > 0 ? H - BAR_H - 1 : 1;
    int ya = cy0 > BAR_H ? cy0 : BAR_H;
    for (int y = ya; y < cy1; y++) {
        uint32_t c = blend(bgs[bg_choice].top, bgs[bg_choice].bot, y - BAR_H, span);
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
    { "backdrop", "the next one", "" },                     /* (v0.60.169) not a program: the background */
};
#define MENU_N (int)(sizeof(menu_item) / sizeof(menu_item[0]))
#define MENU_H (MENU_N * MENU_IH + 4)
static int menu_open;
static void draw_menu(void) {                     /* within the clip */
    fill(MENU_X, MENU_Y, MENU_W, MENU_H, EDGE_RGB);
    fill(MENU_X + 1, MENU_Y, MENU_W - 2, MENU_H - 1, 0x00F4F1EAu);
    for (int i = 0; i < MENU_N; i++) {
        int y = MENU_Y + 2 + i * MENU_IH + (MENU_IH - 16) / 2;
        text(MENU_X + 10, y, menu_item[i][0], 0x00202020u);
        text(MENU_X + 10 + 8 * 8, y, menu_item[i][1], 0x00606870u);
    }
}
static void repaint(int x, int y, int w, int h) {
    cx0 = x < 0 ? 0 : x; cy0 = y < BAR_H ? BAR_H : y;
    cx1 = x + w > W ? W : x + w; cy1 = y + h > H - TASK_H ? H - TASK_H : y + h;
    if (cx0 < cx1 && cy0 < cy1) {
        background();
        for (int i = 0; i < nwin; i++) if (!win[order[i]].min) draw_window(order[i]);
        if (menu_open) draw_menu();
    }
    clip_all();
}
static int ct_uid;                                 /* whose desktop (v0.60.143) */
static void settings_path(char *p) {               /* the cook's `countertop` file */
    if (ct_uid == 0) { strcpy(p, "/countertop"); return; }
    strcpy(p, "/home/");
    strncpy(p + 6, users_name_of((uint8_t)ct_uid), FAT_PATH_MAX - 20);
    p[FAT_PATH_MAX - 14] = '\0';
    strcat(p, "/countertop");
}
static void load_settings(void) {
    char p[FAT_PATH_MAX], buf[64];
    uint32_t n = 0;
    settings_path(p);
    bg_choice = 0;
    if (fat_read(p, (uint8_t *)buf, sizeof(buf) - 1, &n) < 0) return;
    buf[n < sizeof(buf) - 1 ? n : sizeof(buf) - 1] = '\0';
    if (!strncmp(buf, "background ", 11) && buf[11] >= '0' && buf[11] < '0' + NBG) bg_choice = buf[11] - '0';
    klog("[countertop] settings from %s: background %s\n", p, bgs[bg_choice].name);
}
static void next_background(void) {
    bg_choice = (bg_choice + 1) % NBG;
    repaint(0, BAR_H, W, H - BAR_H - TASK_H);
    char p[FAT_PATH_MAX], line[16] = "background 0\n";
    line[11] = (char)('0' + bg_choice);
    settings_path(p);
    int ok = users_may(p, 'w') && fat_write(p, (const uint8_t *)line, 13) >= 0;
    klog("[countertop] background %s, %s %s\n", bgs[bg_choice].name, ok ? "kept in" : "not kept in", p);
}
static void menu_set(int open) {
    if (menu_open == open) return;
    menu_open = open;
    repaint(MENU_X, MENU_Y, MENU_W, MENU_H);
    klog("[countertop] kitchen menu %s\n", open ? "open" : "closed");
}
static void repaint_win(int k) { repaint(win[k].x, win[k].y, win[k].w, win[k].h); }

static int open_window(int x, int y, int w, int h, const char *title, uint32_t body, const char *const *lines) {
    for (int k = 0; k < MAX_WIN; k++) if (!win[k].used) {
        win_t *v = &win[k];
        v->used = 1; v->x = x; v->y = y; v->w = w; v->h = h; v->body = body; v->term = 0; v->sess = 0; v->min = 0; v->files = 0; v->app = -1; v->maxed = 0; v->snapped = 0;
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
static void close_window(int k);
static int nterm;
static int open_terminal(void) {
    wterm_t *t = wt_new();
    if (!t) return -1;
    static const char *const none[] = { 0 };
    int ww = TCOLS * 8 + 2 * BORDER, wh = TROWS * 16 + 2 * BORDER + TITLE_H;
    int off = (nterm % 6) * 28;
    char title[32] = "Terminal ";
    int n = ++nterm, l = 9;
    if (n >= 10) title[l++] = (char)('0' + n / 10 % 10);
    title[l++] = (char)('0' + n % 10); title[l] = '\0';
    int k = open_window(60 + off, 60 + off, ww, wh, title, 0, none);
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
    int k = open_window(W - 420, 60, 380, 460, "Files", 0, none);
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
    int t = open_terminal();
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
 * are skipped because the focus skips them. Alt is read when the Tab
 * arrives: measured held for QEMU's alt-tab (it holds keys 100 ms; the
 * loop reads within ~20), as it is for a hand on the keys. */
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
static void close_window(int k) {
    if (win[k].files) { kfree(win[k].files); win[k].files = 0; }
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
        if (!win[i].used) continue;
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
        if (!win[i].used) continue;
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
    fill(NOTE_X, 0, W - 8 * 8 - 12 - NOTE_X, BAR_H, BAR_RGB);
    if (msg) text(NOTE_X, (BAR_H - 16) / 2, msg, 0x00F0B040u);
}
static void ask_leave(void) {
    int n = apps_open();
    if (!n || (leave_armed && timer_get_ticks() - leave_armed_at < 500)) { leave_asked = 1; return; }
    leave_armed = 1; leave_armed_at = timer_get_ticks();
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
 * path is typed there, quoted, with no Enter; let go anywhere else, or
 * hardly moved, nothing happens. */
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
    if (fd_win >= 0 && (!left || np)) {                /* a files row let go */
        int k = window_at(px, py), from = fd_win;
        fd_win = -1;
        int moved = (px - fd_x) * (px - fd_x) + (py - fd_y) * (py - fd_y) > 64;
        if (moved && k >= 0 && k != from && win[k].term && win[from].files) {
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
    if (np && menu_open) {                             /* a click with the menu down: an item, or away */
        int i = (py - MENU_Y - 2) / MENU_IH;
        int on_button = py < BAR_H && px >= BTN_KITCHEN_X && px < BTN_KITCHEN_X + BTN_KITCHEN_W;
        menu_set(0);
        if (px >= MENU_X && px < MENU_X + MENU_W && py >= MENU_Y + 2 && i >= 0 && i < MENU_N && !menu_item[i][2][0]) {
            next_background();
        } else if (px >= MENU_X && px < MENU_X + MENU_W && py >= MENU_Y + 2 && i >= 0 && i < MENU_N) {
            int t = open_terminal();
            if (t >= 0) {
                char line[40] = "cook ";
                scopy(line + 5, menu_item[i][2], (int)sizeof(line) - 6);
                for (const char *c = line; *c; c++) wt_key(win[t].term, *c);
                wt_key(win[t].term, '\n');
                klog("[countertop] kitchen: %s\n", menu_item[i][0]);
            }
        }
        if (on_button || (px >= MENU_X && px < MENU_X + MENU_W && py < MENU_Y + MENU_H)) return;
    }
    if (np && py < BAR_H) {
        if (px >= BTN_KITCHEN_X && px < BTN_KITCHEN_X + BTN_KITCHEN_W) { menu_set(1); return; }
        if (px >= BTN_TERM_X && px < BTN_TERM_X + BTN_TERM_W) open_terminal();
        else if (px >= BTN_LEAVE_X && px < BTN_LEAVE_X + BTN_LEAVE_W) ask_leave();
        else if (px >= BTN_FILES_X && px < BTN_FILES_X + BTN_FILES_W) open_files("/");
        return;
    }
    if (np && py >= H - TASK_H) {
        int k = taskbar_hit(px, py);
        if (k == -2) open_terminal();
        else if (k >= 0 && win[k].min) restore_window(k);
        else if (k >= 0 && k == topmost()) minimise_window(k);
        else if (k >= 0) raise_window(k);
        return;
    }
    if (np) {
        int k = window_at(px, py);
        if (k < 0) return;
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
    rtc_read(&t);
    if ((int)t.second == clock_sec) return;
    clock_sec = (int)t.second;
    char s[9] = { (char)('0' + t.hour / 10), (char)('0' + t.hour % 10), ':', (char)('0' + t.minute / 10),
                  (char)('0' + t.minute % 10), ':', (char)('0' + t.second / 10), (char)('0' + t.second % 10), 0 };
    fill(W - 8 * 8 - 12, 0, 8 * 8 + 12, BAR_H, BAR_RGB);
    text(W - 8 * 8 - 8, (BAR_H - 16) / 2, s, TEXT_RGB);
}

static void button(int x, int w, const char *label) {
    fill(x, 3, w, BAR_H - 6, BTN_RGB);
    text(x + 8, (BAR_H - 16) / 2, label, TEXT_RGB);
}
static void bar(void) {
    fill(0, 0, W, BAR_H, BAR_RGB);
    text(10, (BAR_H - 16) / 2, "soupOS", TEXT_RGB);
    text(10 + 8 * 7, (BAR_H - 16) / 2, "countertop", DIM_RGB);
    button(BTN_TERM_X, BTN_TERM_W, "Terminal");
    button(BTN_LEAVE_X, BTN_LEAVE_W, "Leave");
    button(BTN_FILES_X, BTN_FILES_W, "Files");
    button(BTN_KITCHEN_X, BTN_KITCHEN_W, "Kitchen");
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
    load_settings();                                 /* before the first background (v0.60.169) */
    background();
    bar();
    nwin = 0; drag = -1; last_btn = 0; leave_asked = 0; leave_armed = 0; menu_open = 0; rs_win = -1; sel_win = -1; fd_win = -1; ct_uid = uid; nterm = 0; task_sig = 0xFFFFFFFFu;
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
    static const char *const pantry[] = { "The pantry.", "", "Windows of their own come next:",
                                          "terminals, files, programs.", 0 };
    open_window(120, 100, 420, 220, "Welcome", 0x00F4F1EAu, welcome);
    open_window(420, 260, 360, 240, "Pantry", 0x00E6EEF5u, pantry);
    int b;
    mouse_get_px(&ptr_x, &ptr_y, &b);
    last_btn = b;
    presses_seen = mouse_presses(0, 0);
    wheel_seen = mouse_wheel();
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
            } else if (k >= 0 && win[k].files) {
                struct files *f = win[k].files;
                int rows = files_rows(&win[k]), most = f->n > rows ? f->n - rows : 0;
                int top = f->top + 3 * dw;
                if (top > most) top = most;
                if (top < 0) top = 0;
                if (top != f->top) { f->top = top; repaint_win(k); klog("[countertop] files scrolled to row %d\n", top); }
            }
        }
        bar_clock();
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
            if (c == '\t' && keyboard_key_pressed(KEY_SC_LALT) && k >= 0) {   /* Alt+Tab (v0.60.155) */
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
        {                                            /* the taskbar, when the windows changed */
            uint32_t sig = (uint32_t)topmost() * 2654435761u;
            for (int i = 0; i < MAX_WIN; i++) sig = sig * 31u + (uint32_t)(win[i].used * 2 + win[i].min);
            if (sig != task_sig) { task_sig = sig; taskbar(); }
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
    fb_clear(0x00101828);                 /* the console's border, as vga13h leaves it */
    fbcon_resume();
    klog("[countertop] down\n");
    return 0;
}

#include "vga.h"
#include "str.h"
#include "console.h"
#include "task.h"

/* Text-mode output is shared state: vga_row, vga_col and vga_attr, plus the
 * cell buffer itself. Since v0.7.4 the timer can switch tasks anywhere, so a
 * background task printing mid-line would interleave characters and leave the
 * cursor wherever the other task moved it.
 *
 * preempt_disable is the right tool here, unlike in fat.c: nothing on this
 * path yields (serial_putc busy-waits on THR-empty), so suppressing the timer
 * really does make the region atomic. It is a per-task recursive count, so
 * vga_puts nesting into vga_putchar is fine. */

#include <stdarg.h>

/* The geometry is runtime state, not a constant, because the framebuffer
 * console is larger than the hardware text mode. Everything below keeps using
 * VGA_WIDTH and VGA_HEIGHT, which now read these. */
static int v_cols = 80, v_rows = 25;
#define VGA_WIDTH   v_cols
#define VGA_HEIGHT  v_rows

int  vga_cols(void) { return v_cols; }
int  vga_rows(void) { return v_rows; }


/* The cell store. On an ordinary boot this is the text-mode hardware at
 * 0xB8000. On a framebuffer boot that window is DEAD - reads come back as all
 * ones and writes are discarded, because the card is scanning out the linear
 * framebuffer and does not back the legacy window with memory - so the store
 * is switched to a RAM array before the first character is printed, and the
 * framebuffer console renders from there. Everything in this file goes
 * through the pointer; so do the editor and the stress test, via vga_cells().
 */
static uint16_t *vga_buffer = (uint16_t *)0xB8000;
static int hw_cursor = 1;        /* cleared by vga_set_backing: no ports on a RAM store */
static int cursor_dirty;
#define VGA_BUFFER  vga_buffer

uint16_t *vga_cells(void) { return vga_buffer; }
void vga_set_backing(uint16_t *cells) { vga_buffer = cells; hw_cursor = 0; }

/* I/O port helpers */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static int vga_row;
static int vga_col;

void vga_set_geometry(int cols, int rows) {
    if (cols < 1 || rows < 1) return;
    if (cols > VGA_COLS_MAX || rows > VGA_ROWS_MAX) return;
    v_cols = cols;
    v_rows = rows;
    if (vga_row >= rows) vga_row = rows - 1;
    if (vga_col >= cols) vga_col = cols - 1;
}

static uint8_t vga_attr;  /* high nibble = bg, low nibble = fg */

static uint8_t make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

static uint16_t make_entry(char c, uint8_t attr) {
    return (uint16_t)((uint16_t)attr << 8 | (uint8_t)c);
}

/* The hardware cursor is four port writes, and under a hypervisor every port
 * write is a VM exit, so doing it per character made a screenful of text cost
 * tens of milliseconds. It is now done once per string (the public wrappers
 * flush it), and not at all on a framebuffer boot, where the cell store is RAM
 * and fbcon draws the cursor itself from vga_get_row/col. */

static void update_hw_cursor(void) {
    cursor_dirty = 0;
    if (!hw_cursor) return;
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    /* Move the top 24 rows up, a 32-bit word (two cells) at a time.
     *
     * NOT memmove: this file's memmove is a byte loop, so handing it 3840
     * bytes does twice the work of the 1920 cell copies this replaced, and
     * measured 47% SLOWER. Two cells per store is half the original. 0xB8000
     * and the +160 source are both 4-byte aligned, and VGA_WIDTH is even, so
     * the row block divides exactly into words. The destination is below the
     * source, so copying upward never overwrites unread data. */
    uint32_t       *d = (uint32_t *)VGA_BUFFER;
    const uint32_t *b = (const uint32_t *)(VGA_BUFFER + VGA_WIDTH);
    uint32_t words = (uint32_t)(VGA_HEIGHT - 1) * VGA_WIDTH / 2;
    for (uint32_t i = 0; i < words; i++) d[i] = b[i];
    /* Blank the last row */
    for (int col = 0; col < VGA_WIDTH; col++) {
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + col] =
            make_entry(' ', vga_attr);
    }
    vga_row = VGA_HEIGHT - 1;
}

/* Scroll self-test, for `sample`.
 *
 * The scroll is a hand-written word copy over the frame buffer, so it is worth
 * checking it moves exactly what it should: a wrong word count or a bad
 * alignment assumption would shift the screen by half a cell, which looks
 * like corruption and is easy to mistake for a font problem. Saves the real
 * screen, runs the check on a known pattern, puts the screen back. */
static uint16_t scroll_test_save[VGA_COLS_MAX * VGA_ROWS_MAX];

int vga_scroll_selftest(void) {
    preempt_disable();
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        scroll_test_save[i] = VGA_BUFFER[i];
    int saved_row = vga_row, saved_col = vga_col;

    /* Every cell carries its own row number, so a shift of any size shows. */
    for (int r = 0; r < VGA_HEIGHT; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA_BUFFER[r * VGA_WIDTH + c] = (uint16_t)(r * VGA_WIDTH + c);

    scroll();

    int ok = 1;
    for (int r = 0; r < VGA_HEIGHT - 1 && ok; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            if (VGA_BUFFER[r * VGA_WIDTH + c] != (uint16_t)((r + 1) * VGA_WIDTH + c)) { ok = 0; break; }
    for (int c = 0; c < VGA_WIDTH && ok; c++)
        if ((VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + c] & 0xFF) != ' ') ok = 0;

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = scroll_test_save[i];
    vga_row = saved_row; vga_col = saved_col;
    preempt_enable();
    return ok;
}

void vga_init(void) {
    vga_attr = make_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_row  = 0;
    vga_col  = 0;
    vga_clear();
}

static void vga_clear_impl(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = make_entry(' ', vga_attr);
    vga_row = 0;
    vga_col = 0;
    update_hw_cursor();
}

static void vga_set_color_impl(vga_color_t fg, vga_color_t bg) {
    vga_attr = make_color(fg, bg);
}

static void vga_set_cursor_impl(int row, int col) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
    if (col >= VGA_WIDTH)  col = VGA_WIDTH  - 1;
    vga_row = row;
    vga_col = col;
    update_hw_cursor();
}

int vga_get_row(void) { return vga_row; }
int vga_get_col(void) { return vga_col; }

static void vga_fill_row_impl(int row, char c) {
    if (row < 0 || row >= VGA_HEIGHT) return;
    uint16_t cell = make_entry(c, vga_attr);
    for (int col = 0; col < VGA_WIDTH; col++)
        VGA_BUFFER[row * VGA_WIDTH + col] = cell;
}

/* Set while printing bytes that are GLYPH CODES rather than text - the boot
 * logo's CP437 blocks - which a serial terminal would render as noise. The
 * caller sends its own encoding to the log instead. */
static int mirror_suppressed;

static void vga_putchar_impl(char c) {
    /* Mirror to the serial console before touching the cell buffer, so a
     * remote client sees exactly the stream a local VGA user would. */
    if (!mirror_suppressed) console_out_char(c);

    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
        } else if (vga_row > 0) {
            vga_row--;
            vga_col = VGA_WIDTH - 1;
        } else {
            /* At top-left - nothing to erase */
            cursor_dirty = 1;
            return;
        }
        VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] = make_entry(' ', vga_attr);
    } else if (c == '\t') {
        /* Align to next 8-column tab stop */
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    } else {
        VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] = make_entry(c, vga_attr);
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    }

    if (vga_row >= VGA_HEIGHT)
        scroll();

    cursor_dirty = 1;
}

static inline void flush_cursor(void) { if (cursor_dirty) update_hw_cursor(); }

static void vga_puts_impl(const char *s) {
    while (*s)
        vga_putchar(*s++);
}

/* printf subset: %[0][width]s/c/d/u/x/X/p/%
   Supports zero-padding and minimum field width, e.g. %08x, %02u. */
static void vga_emit(char c, void *ctx) { (void)ctx; vga_putchar_impl(c); }

void vga_printf(const char *fmt, ...) {
    preempt_disable();   /* keep a whole formatted line together */
    va_list args;
    va_start(args, fmt);
    kvformat(vga_emit, 0, fmt, args);
    va_end(args);
    flush_cursor();
    preempt_enable();
}

/* ── Serialised public entry points ───────────────────────────────────────
 * Wrappers rather than edits to each return path: vga_putchar has an early
 * exit in its backspace case, and a missed preempt_enable would leave the
 * task permanently unpreemptable. */
void vga_clear(void) {
    preempt_disable();
    vga_clear_impl();
    preempt_enable();
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    preempt_disable();
    vga_set_color_impl(fg, bg);
    preempt_enable();
}

void vga_set_cursor(int row, int col) {
    preempt_disable();
    vga_set_cursor_impl(row, col);
    preempt_enable();
}

void vga_fill_row(int row, char c) {
    preempt_disable();
    vga_fill_row_impl(row, c);
    preempt_enable();
}

void vga_putchar(char c) {
    preempt_disable();
    vga_putchar_impl(c);
    flush_cursor();
    preempt_enable();
}

void vga_puts(const char *s) {
    preempt_disable();
    vga_puts_impl(s);
    flush_cursor();
    preempt_enable();
}

void vga_set_mirror(int on) { mirror_suppressed = !on; }

void vga_puts_screen_only(const char *s) {
    preempt_disable();
    mirror_suppressed = 1;
    vga_puts_impl(s);
    mirror_suppressed = 0;
    flush_cursor();
    preempt_enable();
}

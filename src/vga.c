#include "vga.h"
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

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_BUFFER  ((uint16_t *)0xB8000)

/* I/O port helpers */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static int vga_row;
static int vga_col;
static uint8_t vga_attr;  /* high nibble = bg, low nibble = fg */

static uint8_t make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

static uint16_t make_entry(char c, uint8_t attr) {
    return (uint16_t)((uint16_t)attr << 8 | (uint8_t)c);
}

static void update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    /* Move every row up one */
    for (int row = 1; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            VGA_BUFFER[(row - 1) * VGA_WIDTH + col] =
                VGA_BUFFER[row * VGA_WIDTH + col];
        }
    }
    /* Blank the last row */
    for (int col = 0; col < VGA_WIDTH; col++) {
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + col] =
            make_entry(' ', vga_attr);
    }
    vga_row = VGA_HEIGHT - 1;
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

static void vga_putchar_impl(char c) {
    /* Mirror to the serial console before touching the cell buffer, so a
     * remote client sees exactly the stream a local VGA user would. */
    console_out_char(c);

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
            update_hw_cursor();
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

    update_hw_cursor();
}

static void vga_puts_impl(const char *s) {
    while (*s)
        vga_putchar(*s++);
}

/* printf subset: %[0][width]s/c/d/u/x/X/p/%
   Supports zero-padding and minimum field width, e.g. %08x, %02u. */
void vga_printf(const char *fmt, ...) {
    preempt_disable();   /* keep a whole formatted line together */
    va_list args;
    va_start(args, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { vga_putchar(*p); continue; }
        p++;

        /* Flags: left-align then zero-pad. Without the '-' case a format
         * like "%-10s" fell through the switch and printed literally, which
         * is how `pantry` ended up listing every PCI device as "%-10s". */
        int left_align = 0;
        if (*p == '-') { left_align = 1; p++; }
        int zero_pad = 0;
        if (*p == '0') { zero_pad = 1; p++; }

        /* Width */
        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }

        /* Helper: emit a reversed digit buffer with padding */
        /* (buf[0..len-1] holds digits in reverse order) */
        #define EMIT_NUM(buf, len, pad_char) do {           \
            int _l = (len), _w = width;                     \
            char _pc = (pad_char);                          \
            if (!left_align)                                \
                for (int _k = _l; _k < _w; _k++) vga_putchar(_pc); \
            for (int _j = _l - 1; _j >= 0; _j--) vga_putchar((buf)[_j]); \
            if (left_align)                                 \
                for (int _k = _l; _k < _w; _k++) vga_putchar(' '); \
        } while (0)

        switch (*p) {
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                int slen = 0;
                while (s[slen]) slen++;
                if (!left_align)
                    for (int k = slen; k < width; k++) vga_putchar(' ');
                vga_puts(s);
                if (left_align)
                    for (int k = slen; k < width; k++) vga_putchar(' ');
                break;
            }
            case 'c':
                vga_putchar((char)va_arg(args, int));
                break;
            case 'd': {
                int val = va_arg(args, int);
                int neg = (val < 0);
                unsigned uval = neg ? (unsigned)(-(val + 1)) + 1u : (unsigned)val;
                char buf[12]; int len = 0;
                if (uval == 0) { buf[len++] = '0'; }
                else { while (uval) { buf[len++] = '0' + (uval % 10); uval /= 10; } }
                if (neg) vga_putchar('-');
                EMIT_NUM(buf, len, zero_pad ? '0' : ' ');
                break;
            }
            case 'u': {
                unsigned val = va_arg(args, unsigned);
                char buf[12]; int len = 0;
                if (val == 0) { buf[len++] = '0'; }
                else { while (val) { buf[len++] = '0' + (val % 10); val /= 10; } }
                EMIT_NUM(buf, len, zero_pad ? '0' : ' ');
                break;
            }
            case 'x': case 'X': case 'p': {
                unsigned val;
                if (*p == 'p') {
                    val = (unsigned)(uintptr_t)va_arg(args, void *);
                    zero_pad = 1; width = 8;   /* always show full pointer */
                } else {
                    val = va_arg(args, unsigned);
                }
                const char *hex = (*p == 'X') ? "0123456789ABCDEF"
                                               : "0123456789abcdef";
                char buf[9]; int len = 0;
                if (val == 0) { buf[len++] = '0'; }
                else { while (val) { buf[len++] = hex[val & 0xF]; val >>= 4; } }
                EMIT_NUM(buf, len, zero_pad ? '0' : ' ');
                break;
            }
            case '%':
                vga_putchar('%');
                break;
            default:
                vga_putchar('%');
                vga_putchar(*p);
                break;
        }
        #undef EMIT_NUM
    }

    va_end(args);
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
    preempt_enable();
}

void vga_puts(const char *s) {
    preempt_disable();
    vga_puts_impl(s);
    preempt_enable();
}

/* term.c - the terminal abstraction, and the local console as a terminal.
 * See term.h. */
#include "term.h"
#include "vga.h"
#include "keyboard.h"
#include "str.h"
#include <stdarg.h>

/* ── the local console ─────────────────────────────────────────────────── */
static void v_puts(term_t *t, const char *s)             { (void)t; vga_puts(s); }
static void v_putc(term_t *t, char c)                    { (void)t; vga_putchar(c); }
static void v_color(term_t *t, int fg, int bg)           { (void)t; vga_set_color((vga_color_t)fg, (vga_color_t)bg); }
static void v_cursor(term_t *t, int row, int col)        { (void)t; vga_set_cursor(row, col); }
static int  v_row(term_t *t)                             { (void)t; return vga_get_row(); }
static int  v_col(term_t *t)                             { (void)t; return vga_get_col(); }
static int  v_cols(term_t *t)                            { (void)t; return vga_cols(); }
static int  v_rows(term_t *t)                            { (void)t; return vga_rows(); }
static void v_clear(term_t *t)                           { (void)t; vga_clear(); }
static int  v_getc(term_t *t)                            { (void)t; return keyboard_getchar(); }
static int  v_avail(term_t *t)                           { (void)t; return keyboard_available(); }
static void v_flush(term_t *t)                           { (void)t; keyboard_flush(); }

term_t term_vga = {
    "console", v_puts, v_putc, v_color, v_cursor, v_row, v_col, v_cols, v_rows,
    v_clear, v_getc, v_avail, v_flush, 0
};

/* ── generic helpers ───────────────────────────────────────────────────── */
void term_puts(term_t *t, const char *s) { t->puts(t, s); }

/* Format into a buffer and hand the terminal one string, so a formatted line
 * stays together the way vga_printf kept it together. A line longer than the
 * buffer goes in pieces. The formatter is kvformat, the one vga_printf uses,
 * so the bytes are identical. */
typedef struct { term_t *t; char buf[512]; uint32_t n; } fmtbuf_t;

static void fb_emit(char c, void *ctx) {
    fmtbuf_t *f = ctx;
    if (f->n == sizeof(f->buf) - 1) { f->buf[f->n] = 0; f->t->puts(f->t, f->buf); f->n = 0; }
    f->buf[f->n++] = c;
}

void term_printf(term_t *t, const char *fmt, ...) {
    fmtbuf_t local;                    /* on the stack: shells must not share it */
    local.t = t; local.n = 0;
    va_list ap;
    va_start(ap, fmt);
    kvformat(fb_emit, &local, fmt, ap);
    va_end(ap);
    local.buf[local.n] = 0;
    if (local.n) t->puts(t, local.buf);
}

/* term.c - the terminal abstraction, and the local console as a terminal.
 * See term.h. */
#include "term.h"
#include "vga.h"
#include "keyboard.h"
#include "str.h"
#include "task.h"
#include "proc.h"
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
    v_clear, v_getc, v_avail, v_flush,
    0,                                   /* put_cell: jot draws the console itself */
    0, 0, 0
};

term_t *term_current(void) {
    task_t *t = task_current();
    return (t && t->term) ? t->term : &term_vga;
}

/* ── generic helpers ───────────────────────────────────────────────────── */
void term_puts(term_t *t, const char *s)      { t->puts(t, s); }
void term_putc(term_t *t, char c)             { t->putc(t, c); }
void term_color(term_t *t, int fg, int bg)    { t->set_color(t, fg, bg); }

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

/* ── the byte-stream terminal ──────────────────────────────────────────── */

typedef struct { term_stream_t *s; char buf[256]; uint32_t n; } out_t;

static void out_flush(out_t *o) {
    if (o->n && !o->s->closed) o->s->write(o->buf, o->n, o->s->wctx);
    o->n = 0;
}
static void out_byte(out_t *o, char c) {
    if (o->n == sizeof(o->buf)) out_flush(o);
    o->buf[o->n++] = c;
}
static void out_str(out_t *o, const char *z) { while (*z) out_byte(o, *z++); }
static void out_num(out_t *o, int v) {
    char d[12]; int n = 0;
    if (v <= 0) { out_byte(o, '0'); return; }
    while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) out_byte(o, d[--n]);
}

static void newline(out_t *o) {
    term_stream_t *s = o->s;
    out_str(o, "\r\n");
    s->col = 0;
    if (s->row < s->rows - 1) s->row++;
}

/* One character of output, tracked. */
static void stream_char(out_t *o, char c) {
    term_stream_t *s = o->s;
    if (c == '\n') { newline(o); return; }
    if (c == '\r') { out_byte(o, '\r'); s->col = 0; return; }
    if (c == '\b') {                        /* the console erases the cell it backs over */
        if (s->col > 0) { out_str(o, "\b \b"); s->col--; }
        return;
    }
    if (c == '\t') {
        do { stream_char(o, ' '); } while (s->col % 8);
        return;
    }
    if ((unsigned char)c < ' ') return;     /* no stray control bytes to the far end */
    out_byte(o, c);
    if (++s->col >= s->cols) newline(o);    /* our own wrap: the tracker stays exact */
}

static term_stream_t *S_(term_t *t) { return (term_stream_t *)t; }

static void s_puts(term_t *t, const char *z) {
    out_t o; o.s = S_(t); o.n = 0;
    while (*z) stream_char(&o, *z++);
    out_flush(&o);
}
static void s_putc(term_t *t, char c) {
    out_t o; o.s = S_(t); o.n = 0;
    stream_char(&o, c);
    out_flush(&o);
}

/* VGA palette index -> ANSI colour index (0 black, 1 red, 2 green, 3 yellow,
 * 4 blue, 5 magenta, 6 cyan, 7 white); 8-15 are the bright halves. */
static const uint8_t vga_to_ansi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

static void s_color(term_t *t, int fg, int bg) {
    term_stream_t *s = S_(t);
    if (fg == s->fg_col && bg == s->bg_col) return;
    s->fg_col = fg; s->bg_col = bg;
    out_t o; o.s = s; o.n = 0;
    out_str(&o, "\033[0");
    if (fg != 7) {                          /* light grey is "the user's default" */
        int a = vga_to_ansi[fg & 7];
        out_byte(&o, ';'); out_num(&o, (fg & 8) ? 90 + a : 30 + a);
    }
    if (bg != 0) {                          /* black is "the user's default" */
        int a = vga_to_ansi[bg & 7];
        out_byte(&o, ';'); out_num(&o, (bg & 8) ? 100 + a : 40 + a);
    }
    out_byte(&o, 'm');
    out_flush(&o);
}

static void s_cursor(term_t *t, int row, int col) {
    term_stream_t *s = S_(t);
    if (row < 0) row = 0;
    if (row > s->rows - 1) row = s->rows - 1;
    if (col < 0) col = 0;
    if (col > s->cols - 1) col = s->cols - 1;
    out_t o; o.s = s; o.n = 0;
    int dr = row - s->row;
    if (dr < 0) { out_str(&o, "\033["); out_num(&o, -dr); out_byte(&o, 'A'); }
    if (dr > 0) { out_str(&o, "\033["); out_num(&o,  dr); out_byte(&o, 'B'); }
    if (col == 0 && s->col != 0) out_byte(&o, '\r');
    else if (col > s->col) { out_str(&o, "\033["); out_num(&o, col - s->col); out_byte(&o, 'C'); }
    else if (col < s->col) { out_str(&o, "\033["); out_num(&o, s->col - col); out_byte(&o, 'D'); }
    s->row = row; s->col = col;
    out_flush(&o);
}

static int  s_row (term_t *t) { return S_(t)->row; }
static int  s_col (term_t *t) { return S_(t)->col; }
static int  s_cols(term_t *t) { return S_(t)->cols; }
static int  s_rows(term_t *t) { return S_(t)->rows; }

static void s_clear(term_t *t) {
    term_stream_t *s = S_(t);
    out_t o; o.s = s; o.n = 0;
    out_str(&o, "\033[2J\033[H");
    out_flush(&o);
    s->row = s->col = 0;
}

static int s_avail(term_t *t) {
    term_stream_t *s = S_(t);
    return s->closed || s->head != s->tail;
}

static int s_getc(term_t *t) {
    term_stream_t *s = S_(t);
    while (s->head == s->tail) {
        if (s->closed) return -1;
        task_yield();
        if (s->head == s->tail && !s->closed) cpu_halt();
    }
    int k = s->ring[s->tail % 128];
    s->tail++;
    return k;
}

static void s_flush(term_t *t) { term_stream_t *s = S_(t); s->tail = s->head; }

static void stream_key(int key, void *ctx) {
    term_stream_t *s = ctx;
    /* Ctrl-C and Ctrl-Z belong to the foreground program if there is one,
     * as the keyboard IRQ arranges for the console; with none, they go to
     * the shell, which uses Ctrl-C to cancel the line. proc_kill rather than
     * the flag alone, because the program may be blocked and needs waking. */
    if (key == 0x03) s->t.intr++;             /* for shell loops (v0.60.1) */
    if ((key == 0x03 || key == 0x1A) && s->t.fg) {
        proc_t *fg = s->t.fg;
        if (key == 0x03) proc_kill(fg->pid);
        else             proc_flag_stop(fg);
        return;
    }
    if (s->head - s->tail >= 128) return;   /* full: drop, as a UART would */
    s->ring[s->head % 128] = key;
    s->head++;
}

void term_stream_init(term_stream_t *s, int cols, int rows,
                      void (*write)(const char *z, uint32_t n, void *ctx), void *ctx) {
    memset(s, 0, sizeof(*s));
    s->t.name = "stream";
    s->t.puts = s_puts;     s->t.putc = s_putc;     s->t.set_color = s_color;
    s->t.set_cursor = s_cursor; s->t.row = s_row;   s->t.col = s_col;
    s->t.cols = s_cols;     s->t.rows = s_rows;     s->t.clear = s_clear;
    s->t.getc = s_getc;     s->t.available = s_avail; s->t.flush_in = s_flush;
    s->t.ctx  = s;
    s->write = write; s->wctx = ctx;
    s->cols  = (cols >= 20 && cols <= 500) ? cols : 80;
    s->rows  = (rows >= 5  && rows <= 300) ? rows : 24;
    s->fg_col = 7; s->bg_col = 0;
    s->x.out = stream_key; s->x.ctx = s;
}

void term_stream_feed(term_stream_t *s, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) keyxlate_byte(&s->x, b[i]);
}

void term_stream_resize(term_stream_t *s, int cols, int rows) {
    if (cols >= 20 && cols <= 500) s->cols = cols;
    if (rows >= 5  && rows <= 300) s->rows = rows;
    if (s->col >= s->cols) s->col = s->cols - 1;
    if (s->row >= s->rows) s->row = s->rows - 1;
}

void term_stream_close(term_stream_t *s) { s->closed = 1; }

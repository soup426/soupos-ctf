#pragma once
#include <stdint.h>
#include "console.h"

/* A terminal: where a shell's output goes and its keystrokes come from.
 * Fourth queue, item 2, stage (b).
 *
 * The shell used to call vga_* and keyboard_* directly, which is why there
 * could only be one, on the local console. It now talks to the term_t in its
 * shell_t, and the local console is one implementation (term_vga). A terminal
 * on a byte stream - an SSH channel - is the next one (stage c).
 *
 * Colours are the VGA palette numbers (vga_color_t), since that is what every
 * caller already uses; a stream terminal maps them to ANSI. Rows and columns
 * are the terminal's own. */

typedef struct term term_t;
struct term {
    const char *name;
    void (*puts)      (term_t *t, const char *s);   /* whole string: kept together */
    void (*putc)      (term_t *t, char c);
    void (*set_color) (term_t *t, int fg, int bg);
    void (*set_cursor)(term_t *t, int row, int col);
    int  (*row)       (term_t *t);
    int  (*col)       (term_t *t);
    int  (*cols)      (term_t *t);
    int  (*rows)      (term_t *t);
    void (*clear)     (term_t *t);
    int  (*getc)      (term_t *t);                  /* blocks */
    int  (*available) (term_t *t);
    void (*flush_in)  (term_t *t);
    /* One cell at row,col (a VGA attribute byte), the cursor not moved and
     * nothing scrolled, for a full-screen program; 0 on a terminal that
     * cannot (a stream). jot runs on a terminal that has it (v0.60.154). */
    void (*put_cell)  (term_t *t, int row, int col, char ch, int attr);
    void *ctx;
    struct proc *fg;      /* the program that owns this terminal's input, if any:
                           * where Ctrl-C and Ctrl-Z go, and who stdin belongs
                           * to. The console keeps using proc_foreground() too,
                           * for the keyboard IRQ. (v0.36.0) */
    volatile unsigned intr;   /* Ctrl-C presses, counted (v0.60.1): a shell loop
                               * runs no program between some of its commands,
                               * so it watches this to know it was asked to stop */
};

extern term_t term_vga;          /* the local console: VGA/framebuffer + PS/2 */

/* The calling task's terminal: the console unless the task belongs to a
 * shell on another one. Anything printing on a task's behalf - a program's
 * stdout, a script's `pour`, the self-test - prints here. */
term_t *term_current(void);

void term_puts  (term_t *t, const char *s);
void term_putc  (term_t *t, char c);
void term_color (term_t *t, int fg, int bg);
void term_printf(term_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* ── A terminal on a byte stream (an SSH channel) ─────────────────────────
 * Output is translated for a VT100-style terminal: "\n" becomes CR LF, the
 * VGA palette becomes SGR colours (light grey on black is the user's own
 * default), the line editor's absolute cursor moves become relative ones from
 * a cursor this terminal tracks itself, and it wraps at its own width with
 * an explicit CR LF so the far end and the tracker can never disagree about
 * where the cursor is. Input bytes go through the console's own translator
 * into a key ring; Ctrl-C and Ctrl-Z go to the terminal's foreground program
 * when it has one, exactly as the keyboard IRQ does for the console. */
typedef struct term_stream {
    term_t   t;                                  /* first: a term_stream_t IS a term_t */
    void   (*write)(const char *s, uint32_t n, void *ctx);
    void    *wctx;
    int      cols, rows, row, col, fg_col, bg_col;
    volatile int closed;
    keyxlate_t x;
    int      ring[128];
    volatile uint32_t head, tail;
} term_stream_t;

void term_stream_init  (term_stream_t *s, int cols, int rows,
                        void (*write)(const char *s, uint32_t n, void *ctx), void *ctx);
void term_stream_feed  (term_stream_t *s, const uint8_t *bytes, uint32_t n);
void term_stream_resize(term_stream_t *s, int cols, int rows);
/* The far end has gone: getc returns -1 from now on, available() returns 1
 * so loops waiting for a key end, and output is dropped. */
void term_stream_close (term_stream_t *s);

#pragma once
#include <stdint.h>

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
    void *ctx;
};

extern term_t term_vga;          /* the local console: VGA/framebuffer + PS/2 */

void term_puts  (term_t *t, const char *s);
void term_printf(term_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

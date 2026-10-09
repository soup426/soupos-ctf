#include "editor.h"
#include "vga.h"
#include "keyboard.h"
#include "fat.h"
#include "str.h"
#include "clip.h"
#include "mouse.h"
#include "task.h"
#include "term.h"

/* ── Screen geometry ─────────────────────────────────────────────────────
 * 80×25 text mode. Rows 0..23 show text; row 24 is the status bar. We write
 * cells straight into the VGA buffer so a full redraw never scrolls. */
/* Ask, do not assume: the framebuffer console is larger than the hardware
 * text mode, and the editor has to fill whatever it is given. */
/* (v0.60.154) jot also runs in a terminal window on the desktop: ET is the
 * terminal it was started on, and every cell, the cursor, the keys and the
 * clear go to it when it is not the console. The console path is the one
 * it always had (cells straight into the screen, the PS/2 mouse). */
static term_t *ET;
static int     on_con;
#define VGA_W      (on_con ? vga_cols() : ET->cols(ET))
#define VGA_H      (on_con ? vga_rows() : ET->rows(ET))
#define TEXT_ROWS  24
#define STATUS_ROW 24
#define VGA_MEM    (vga_cells())   /* never 0xB8000: dead on FB boots */

#define ATTR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

static void put_cell(int row, int col, char c, uint8_t attr) {
    if (row < 0 || row >= VGA_H || col < 0 || col >= VGA_W) return;
    if (!on_con) { ET->put_cell(ET, row, col, c, attr); return; }
    VGA_MEM[row * VGA_W + col] = ((uint16_t)attr << 8) | (uint8_t)(unsigned char)c;
}
static int  e_getc(void)  { return on_con ? keyboard_getchar() : ET->getc(ET); }
static int  e_avail(void) { return on_con ? keyboard_available() : ET->available(ET); }

static void put_str(int row, int col, const char *s, uint8_t attr) {
    for (; *s && col < VGA_W; s++, col++) put_cell(row, col, *s, attr);
}

/* ── Buffer ──────────────────────────────────────────────────────────────
 * A flat byte buffer; '\n' delimits lines. Edits shift the tail in place. */
#define EBUF_MAX 8192
static char     ebuf[EBUF_MAX];
static int      elen;          /* bytes used                        */
static int      ecur;          /* cursor offset, 0..elen            */
static int      etop;          /* index of first visible line       */
static int      edirty;        /* unsaved changes?                  */
static char     epath[FAT_PATH_MAX];
static char     emsg[48];      /* transient status message          */
static int      emark;         /* selection anchor, -1 when unset    */

/* The selected span, as [lo, hi). Empty when there is no mark. The mark is an
 * anchor, not a direction: it can sit either side of the cursor. */
static void sel_span(int *lo, int *hi) {
    if (emark < 0) { *lo = *hi = -1; return; }
    if (emark <= ecur) { *lo = emark; *hi = ecur; }
    else               { *lo = ecur;  *hi = emark; }
}

static int total_lines(void) {
    int n = 1;
    for (int i = 0; i < elen; i++) if (ebuf[i] == '\n') n++;
    return n;
}

/* Line index + column of the cursor. */
static void cursor_rc(int *line, int *col) {
    int ln = 0, c = 0;
    for (int i = 0; i < ecur; i++) {
        if (ebuf[i] == '\n') { ln++; c = 0; } else c++;
    }
    *line = ln; *col = c;
}

/* Start offset and length (excluding '\n') of line L. -1 if L is past EOF. */
static int line_bounds(int L, int *start, int *len) {
    int i = 0, ln = 0;
    while (i < elen && ln < L) { if (ebuf[i] == '\n') ln++; i++; }
    if (ln < L) return -1;
    int e = i;
    while (e < elen && ebuf[e] != '\n') e++;
    *start = i; *len = e - i;
    return 0;
}

static void ins_char(char ch) {
    if (elen >= EBUF_MAX - 1) return;          /* full */
    for (int i = elen; i > ecur; i--) ebuf[i] = ebuf[i - 1];
    ebuf[ecur] = ch;
    elen++; ecur++; edirty = 1;
}

static void del_before(void) {                  /* backspace */
    if (ecur <= 0) return;
    for (int i = ecur - 1; i < elen - 1; i++) ebuf[i] = ebuf[i + 1];
    elen--; ecur--; edirty = 1;
}

static void del_at(void) {                       /* delete key */
    if (ecur >= elen) return;
    for (int i = ecur; i < elen - 1; i++) ebuf[i] = ebuf[i + 1];
    elen--; edirty = 1;
}

/* Move up/down keeping the column when possible. */
static void move_vert(int dir) {
    int line, col;
    cursor_rc(&line, &col);
    int target = line + dir;
    if (target < 0 || target >= total_lines()) return;
    int s, len;
    if (line_bounds(target, &s, &len) < 0) return;
    ecur = s + (col < len ? col : len);
}

/* Which byte of the buffer is under a screen cell. Past the end of a line
 * clamps to its end, and past the end of the file clamps to the end of the
 * buffer, so a drag into empty space selects to where the text stops rather
 * than doing nothing. */
static int cell_to_offset(int row, int col) {
    if (row < 0) row = 0;
    if (row >= TEXT_ROWS) row = TEXT_ROWS - 1;
    int s, len;
    if (line_bounds(etop + row, &s, &len) != 0) return elen;
    if (col < 0)   col = 0;
    if (col > len) col = len;
    return s + col;
}

/* Copy the selection to the clipboard. Returns the number of bytes, 0 if
 * there is nothing marked. */
static int sel_copy(void) {
    int lo, hi;
    sel_span(&lo, &hi);
    if (lo < 0 || hi <= lo) return 0;
    clip_set(ebuf + lo, (uint32_t)(hi - lo));
    return hi - lo;
}

/* Delete the selection, leaving the cursor where it began. */
static void sel_delete(void) {
    int lo, hi;
    sel_span(&lo, &hi);
    if (lo < 0 || hi <= lo) return;
    int n = hi - lo;
    for (int i = lo; i + n < elen; i++) ebuf[i] = ebuf[i + n];
    elen -= n;
    ecur  = lo;
    emark = -1;
    edirty = 1;
}

/* Insert the clipboard at the cursor. */
static int clip_paste(void) {
    uint32_t n = clip_len();
    if (n == 0) return 0;
    if (elen + (int)n > EBUF_MAX) n = (uint32_t)(EBUF_MAX - elen);
    if (n == 0) return 0;
    for (int i = elen - 1; i >= ecur; i--) ebuf[i + n] = ebuf[i];
    memcpy(ebuf + ecur, clip_peek(), n);
    elen += (int)n;
    ecur += (int)n;
    edirty = 1;
    return (int)n;
}

static void render(void) {
    uint8_t text = ATTR(VGA_LIGHT_GREY, VGA_BLACK);
    uint8_t tilde = ATTR(VGA_DARK_GREY, VGA_BLACK);

    int line, col;
    cursor_rc(&line, &col);

    /* Keep the cursor line on screen. */
    if (line < etop) etop = line;
    if (line >= etop + TEXT_ROWS) etop = line - TEXT_ROWS + 1;

    /* Blank the text area, then paint visible lines. */
    for (int r = 0; r < TEXT_ROWS; r++) {
        for (int c = 0; c < VGA_W; c++) put_cell(r, c, ' ', text);
        int L = etop + r, s, len;
        if (line_bounds(L, &s, &len) == 0) {
            int sel_lo, sel_hi;
            sel_span(&sel_lo, &sel_hi);
            for (int c = 0; c < len && c < VGA_W; c++) {
                char ch = ebuf[s + c];
                int off = s + c;
                uint8_t a = (sel_lo >= 0 && off >= sel_lo && off < sel_hi)
                                ? ATTR(VGA_BLACK, VGA_LIGHT_GREY)   /* selected */
                                : text;
                put_cell(r, c, (ch >= ' ' && ch < 127) ? ch : ' ', a);
            }
        } else {
            put_cell(r, 0, '~', tilde);   /* past end-of-file */
        }
    }

    /* The pointer, drawn by inverting whatever is under it rather than by
     * owning a glyph, so it never hides a character. */
    if (on_con && mouse_present()) {
        int mx, my, mb;
        mouse_get(&mx, &my, &mb);
        if (my >= 0 && my < TEXT_ROWS && mx >= 0 && mx < VGA_W) {
            uint16_t cell = VGA_MEM[my * VGA_W + mx];
            char ch = (char)(cell & 0xFF);
            put_cell(my, mx, ch, ATTR(VGA_BLACK, VGA_LIGHT_GREY));
        }
    }

    /* Status bar. */
    uint8_t st = ATTR(VGA_BLACK, VGA_LIGHT_GREY);
    for (int c = 0; c < VGA_W; c++) put_cell(STATUS_ROW, c, ' ', st);
    char bar[VGA_W + 1];
    const char *nm = epath;
    /* show just the basename to keep it short */
    for (const char *q = epath; *q; q++) if (*q == '/') nm = q + 1;
    int i = 0;
    bar[i++] = ' ';
    for (const char *q = nm; *q && i < 40; q++) bar[i++] = *q;
    if (edirty && i < 44) { bar[i++] = ' '; bar[i++] = '*'; }
    if (emark >= 0 && i < 40) {
        const char *mk = " [MARK]";
        for (const char *q = mk; *q && i < 47; q++) bar[i++] = *q;
    }
    bar[i] = '\0';
    put_str(STATUS_ROW, 0, bar, st);

    char rc[40];
    /* "Ln L,Col C" right-ish; build manually */
    int j = 0;
    rc[j++] = 'L'; rc[j++] = 'n'; rc[j++] = ' ';
    char num[12];
    int v = line + 1, k = 0;
    if (v == 0) num[k++] = '0'; else { while (v) { num[k++] = '0' + v % 10; v /= 10; } }
    while (k) rc[j++] = num[--k];
    rc[j++] = ','; rc[j++] = 'C'; rc[j++] = ' ';
    v = col + 1; k = 0;
    if (v == 0) num[k++] = '0'; else { while (v) { num[k++] = '0' + v % 10; v /= 10; } }
    while (k) rc[j++] = num[--k];
    rc[j] = '\0';
    put_str(STATUS_ROW, 50, rc, st);
    put_str(STATUS_ROW, 59, "^B mark ^C^X^V ^S ESC", st);

    /* Transient message overrides the help text for one frame. */
    if (emsg[0]) {
        for (int c = 50; c < VGA_W; c++) put_cell(STATUS_ROW, c, ' ', st);
        put_str(STATUS_ROW, 50, emsg, st);
    }

    /* Hardware cursor on the editable cell. */
    int cy = line - etop;
    int cx = col < VGA_W ? col : VGA_W - 1;
    if (on_con) vga_set_cursor(cy, cx);
    else ET->set_cursor(ET, cy, cx);
}

static void do_save(void) {
    int rc = fat_write(epath, (const uint8_t *)ebuf, (uint32_t)elen);
    if (rc < 0) {
        strncpy(emsg, "WRITE FAILED (FAT16 only?)", sizeof(emsg) - 1);
    } else {
        strncpy(emsg, "Saved.", sizeof(emsg) - 1);
        edirty = 0;
    }
    emsg[sizeof(emsg) - 1] = '\0';
}

/* Returns 1 to quit. */
static int confirm_discard(void) {
    if (!edirty) return 1;
    strncpy(emsg, "Discard changes? (y/N)", sizeof(emsg) - 1);
    emsg[sizeof(emsg) - 1] = '\0';
    render();
    int c = e_getc();
    emsg[0] = '\0';
    return (c == 'y' || c == 'Y');
}

static volatile int busy;                        /* one buffer: one jot at a time */

int editor_run(const char *path) {
    term_t *t = term_current();
    if (t != &term_vga && !t->put_cell) return -2;  /* a stream: no cells to draw */
    if (busy) return -1;
    busy = 1;
    ET = t;
    on_con = t == &term_vga;
    strncpy(epath, path, FAT_PATH_MAX - 1);
    epath[FAT_PATH_MAX - 1] = '\0';

    uint32_t sz = 0;
    if (fat_read(epath, (uint8_t *)ebuf, EBUF_MAX - 1, &sz) == 0)
        elen = (int)(sz < EBUF_MAX - 1 ? sz : (uint32_t)(EBUF_MAX - 1));
    else
        elen = 0;                                  /* new file */
    ecur = 0; etop = 0; edirty = 0; emsg[0] = '\0'; emark = -1;

    if (on_con) keyboard_flush();   /* drop the Enter that launched us */
    else ET->flush_in(ET);

    uint32_t last_moves = 0;
    int      last_btn   = 0;
    mouse_stats(&last_moves, 0);

    for (;;) {
        render();

        /* Wait for either a keystroke or a mouse packet. jot used to block in
         * keyboard_getchar, which cannot see the mouse at all. */
        int c = -1;
        for (;;) {
            if (e_avail()) { c = e_getc(); break; }
            if (!on_con) { task_sleep(10); continue; }

            if (mouse_present()) {
                uint32_t moves;
                mouse_stats(&moves, 0);
                if (moves != last_moves) {
                    last_moves = moves;
                    int mx, my, mb;
                    mouse_get(&mx, &my, &mb);

                    if ((mb & 1) && !(last_btn & 1)) {
                        /* Press: drop the mark here and start the selection. */
                        emark = cell_to_offset(my, mx);
                        ecur  = emark;
                        strcpy(emsg, "mark set");
                    } else if (mb & 1) {
                        /* Dragging: the cursor follows, so the highlight the
                         * renderer already knows how to draw grows with it. */
                        ecur = cell_to_offset(my, mx);
                    }
                    last_btn = mb;
                    break;                  /* redraw with the new selection */
                }
            }
            task_yield();
        }
        if (c < 0 && !on_con) break;        /* the window closed under it: a hangup */
        if (c < 0) continue;                /* it was the mouse; go redraw */

        emsg[0] = '\0';                            /* clear transient msg */

        if (c == 27 || c == 0x11) {                /* ESC or Ctrl+Q */
            if (confirm_discard()) break;
            continue;
        }
        if (c == 0x13) { do_save(); continue; }    /* Ctrl+S */

        /* ── Copy and paste ───────────────────────────────────────────────
         * Ctrl+B anchors a mark; the span between it and the cursor is shown
         * highlighted and is what copy and cut act on. Ctrl+C / Ctrl+X / Ctrl+V
         * are the usual three. The clipboard is the system one, so a cut here
         * pastes at the shell prompt too. */
        if (c == 0x02) {                           /* Ctrl+B: set/clear mark */
            if (emark < 0) { emark = ecur; strcpy(emsg, "mark set"); }
            else           { emark = -1;   strcpy(emsg, "mark cleared"); }
            continue;
        }
        if (c == 0x03) {                           /* Ctrl+C: copy */
            int n = sel_copy();
            if (n) { emark = -1; strcpy(emsg, "copied"); }
            else     strcpy(emsg, "nothing marked (Ctrl+B)");
            continue;
        }
        if (c == 0x18) {                           /* Ctrl+X: cut */
            int n = sel_copy();
            if (n) { sel_delete(); strcpy(emsg, "cut"); }
            else     strcpy(emsg, "nothing marked (Ctrl+B)");
            continue;
        }
        if (c == 0x16) {                           /* Ctrl+V: paste */
            int n = clip_paste();
            strcpy(emsg, n ? "pasted" : "clipboard empty");
            continue;
        }

        switch (c) {
            case KEY_LEFT:  if (ecur > 0)    ecur--; break;
            case KEY_RIGHT: if (ecur < elen) ecur++; break;
            case KEY_UP:    move_vert(-1); break;
            case KEY_DOWN:  move_vert(+1); break;
            case KEY_HOME: {
                int s, len; int line, col; cursor_rc(&line, &col);
                if (line_bounds(line, &s, &len) == 0) ecur = s;
                break;
            }
            case KEY_END: {
                int s, len; int line, col; cursor_rc(&line, &col);
                if (line_bounds(line, &s, &len) == 0) ecur = s + len;
                break;
            }
            case KEY_DEL: del_at(); break;
            case '\b':    del_before(); break;
            case '\n':    ins_char('\n'); break;
            case '\t':    ins_char(' '); ins_char(' '); break;
            default:
                if (c >= ' ' && c < 127) ins_char((char)c);
                break;
        }
    }

    if (on_con) { vga_clear(); vga_set_cursor(0, 0); }
    else ET->clear(ET);
    busy = 0;
    return 0;
}

#include "editor.h"
#include "vga.h"
#include "keyboard.h"
#include "fat.h"
#include "str.h"

/* ── Screen geometry ─────────────────────────────────────────────────────
 * 80×25 text mode. Rows 0..23 show text; row 24 is the status bar. We write
 * cells straight into the VGA buffer so a full redraw never scrolls. */
#define VGA_W      80
#define VGA_H      25
#define TEXT_ROWS  24
#define STATUS_ROW 24
#define VGA_MEM    ((volatile uint16_t *)0xB8000)

#define ATTR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

static void put_cell(int row, int col, char c, uint8_t attr) {
    if (row < 0 || row >= VGA_H || col < 0 || col >= VGA_W) return;
    VGA_MEM[row * VGA_W + col] = ((uint16_t)attr << 8) | (uint8_t)(unsigned char)c;
}

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
            for (int c = 0; c < len && c < VGA_W; c++) {
                char ch = ebuf[s + c];
                put_cell(r, c, (ch >= ' ' && ch < 127) ? ch : ' ', text);
            }
        } else {
            put_cell(r, 0, '~', tilde);   /* past end-of-file */
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
    put_str(STATUS_ROW, 64, "^S save ESC quit", st);

    /* Transient message overrides the help text for one frame. */
    if (emsg[0]) {
        for (int c = 50; c < VGA_W; c++) put_cell(STATUS_ROW, c, ' ', st);
        put_str(STATUS_ROW, 50, emsg, st);
    }

    /* Hardware cursor on the editable cell. */
    int cy = line - etop;
    int cx = col < VGA_W ? col : VGA_W - 1;
    vga_set_cursor(cy, cx);
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
    int c = keyboard_getchar();
    emsg[0] = '\0';
    return (c == 'y' || c == 'Y');
}

void editor_run(const char *path) {
    strncpy(epath, path, FAT_PATH_MAX - 1);
    epath[FAT_PATH_MAX - 1] = '\0';

    uint32_t sz = 0;
    if (fat_read(epath, (uint8_t *)ebuf, EBUF_MAX - 1, &sz) == 0)
        elen = (int)(sz < EBUF_MAX - 1 ? sz : (uint32_t)(EBUF_MAX - 1));
    else
        elen = 0;                                  /* new file */
    ecur = 0; etop = 0; edirty = 0; emsg[0] = '\0';

    keyboard_flush();   /* drop the Enter that launched us */

    for (;;) {
        render();
        int c = keyboard_getchar();
        emsg[0] = '\0';                            /* clear transient msg */

        if (c == 27 || c == 0x11) {                /* ESC or Ctrl+Q */
            if (confirm_discard()) break;
            continue;
        }
        if (c == 0x13) { do_save(); continue; }    /* Ctrl+S */

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

    vga_clear();
    vga_set_cursor(0, 0);
}

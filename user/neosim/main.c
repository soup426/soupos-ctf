/* main.c - neosim (v0.60.204): a modal editor after neovim, in a window of
 * its own on the desktop. The user's name for it: it simmers.
 *
 *   cook neosim.elf FILE          (from a terminal on the desktop)
 *   cook neosim.elf -wait FILE &  wait up to a minute for the desktop, for
 *                                 tests that start it at the console
 *
 * Normal mode: counts; h j k l and the arrows, w b e 0 ^ $ gg G (v0.60.205);
 * the operators d c y with a motion, dd cc yy, D C; x X, p P, J; u and
 * Ctrl-R; > < (four spaces); / ? n N (plain text); v V with d y c > <; .;
 * % to the matching bracket; Enter, o and O keep the indent (v0.60.218)
 * (v0.60.206); i a A I o O into insert mode; : for a command. Insert mode:
 * typing, Enter, Backspace (joining lines at the start of one), Tab as four
 * spaces, the arrows, Esc back. Commands: :w [FILE], :q, :q!, :wq, :x,
 * :e[!] FILE, :s/A/B/[g] and :%s/A/B/[g] (plain text). :q with changes is
 * refused, as vim's E37. The close box is :q.
 * Each change (an operator, x, a visit to insert mode) is one undo step.
 * Behaviour is checked against neovim's (scripts/neosim-ops-test.sh).
 *
 * Markers on stderr, [neosim] ...: open PATH lines=N (or new PATH), wrote
 * PATH bytes=N, refused :q (changed), bye.
 */
#include "../ulib.h"
#include "ed.h"
#include "motion.h"
#include "undo.h"

static ed_t E;

/* ---- small helpers ---- */
static void eprint_u(unsigned v) {
    char b[12]; int n = 11; b[n] = 0;
    do { b[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    eprint(b + n);
}
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scopy(char *d, const char *s, int n) { int i = 0; while (s[i] && i < n - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void msg(const char *a, const char *b, const char *c) {
    int n = 0;
    for (const char *p = a; p && *p && n < 79; p++) E.msg[n++] = *p;
    for (const char *p = b; p && *p && n < 79; p++) E.msg[n++] = *p;
    for (const char *p = c; p && *p && n < 79; p++) E.msg[n++] = *p;
    E.msg[n] = 0;
}
static void msg_num(const char *a, const char *b, unsigned v, const char *c) {
    char d[12]; int n = 11; d[n] = 0;
    do { d[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    msg(a, b, 0);
    int l = 0; while (E.msg[l]) l++;
    for (const char *p = d + n; *p && l < 79; p++) E.msg[l++] = *p;
    for (const char *p = c; p && *p && l < 79; p++) E.msg[l++] = *p;
    E.msg[l] = 0;
}
static int len(void) { return E.t.len[E.cy]; }
static int lenof(int y) { return E.t.len[y]; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static void clamp(void) {
    if (E.cy >= E.t.n) E.cy = E.t.n - 1;
    if (E.cy < 0) E.cy = 0;
    int max = E.mode == M_INSERT ? len() : (len() > 0 ? len() - 1 : 0);
    if (E.cx > max) E.cx = max;
    if (E.cx < 0) E.cx = 0;
}
static void scroll(void) {
    if (E.cy < E.top) E.top = E.cy;
    if (E.cy >= E.top + TEXT_ROWS) E.top = E.cy - TEXT_ROWS + 1;
    int w = SCR_COLS - NUM_W;
    if (E.cx < E.left) E.left = E.cx;
    if (E.cx >= E.left + w) E.left = E.cx - w + 1;
}

/* ---- changes and undo ---- */
static int cur_change_hook;                         /* set by change_begin: . keeps the command */
static void change_begin(void) {                    /* before every change: a step back to this */
    cur_change_hook = 1;
    undo_push(&E.t, E.cy, E.cx, E.seq);
    E.seq = ++E.idgen;
    E.changed = 1;
}
static void after_undo(void) { E.changed = E.seq != E.saved; clamp(); E.want = E.cx; }

/* ---- the register: what d, c, y and x took; linewise or not ---- */
#define REG_MAX 65536
static char reg[REG_MAX];
static int reg_n, reg_lines;
static void reg_add(char c) { if (reg_n < REG_MAX) reg[reg_n++] = c; }
static void reg_take_lines(int a, int b) {           /* lines a..b, joined by \n */
    reg_n = 0; reg_lines = 1;
    for (int y = a; y <= b; y++) { if (y > a) reg_add('\n'); for (int i = 0; i < lenof(y); i++) reg_add(E.t.s[y][i]); }
}
/* Characters from (ay,ax) to (by,bx), the end not included, across lines. */
static void reg_take_chars(int ay, int ax, int by, int bx) {
    reg_n = 0; reg_lines = 0;
    for (int y = ay; y <= by; y++) {
        int from = y == ay ? ax : 0, to = y == by ? bx : lenof(y);
        for (int i = from; i < to && i < lenof(y); i++) reg_add(E.t.s[y][i]);
        if (y < by) reg_add('\n');
    }
}
static void delete_chars(int ay, int ax, int by, int bx) {
    if (ay == by) { for (int i = ax; i < bx; i++) text_delete_char(&E.t, ay, ax); return; }
    while (lenof(ay) > ax) text_delete_char(&E.t, ay, ax);           /* the rest of the first line */
    for (int y = ay + 1; y < by; y++) text_delete_line(&E.t, ay + 1);  /* the lines between */
    for (int i = 0; i < bx; i++) text_delete_char(&E.t, ay + 1, 0);    /* the start of the last */
    text_join(&E.t, ay);
}
static void delete_lines(int a, int b) {
    for (int y = a; y <= b; y++) text_delete_line(&E.t, a);
}
/* Text (with \n for new lines) in at (y,x); the position after its last character. */
static void insert_text(int *y, int *x, const char *s, int n) {
    for (int i = 0; i < n; i++) {
        if (s[i] == '\n') { if (text_split(&E.t, *y, *x) == 0) { (*y)++; *x = 0; } }
        else if (text_insert_char(&E.t, *y, *x, s[i]) == 0) (*x)++;
    }
}

/* ---- search and substitute: plain text (v0.60.206) ---- */
static char last_pat[80];
static int last_dir = 1;
static int match_at(int y, int x, const char *p, int pl) {
    if (x + pl > lenof(y)) return 0;
    for (int i = 0; i < pl; i++) if (E.t.s[y][x + i] != p[i]) return 0;
    return 1;
}
/* The next match from just after (or before) the cursor, round the end of
 * the text once; 1 found (at *oy,*ox), 0 none. *wrapped: it went round. */
static int find(int dir, const char *p, int *oy, int *ox, int *wrapped) {
    int pl = 0; while (p[pl]) pl++;
    *wrapped = 0;
    if (!pl) return 0;
    int y = E.cy, x = E.cx;
    for (int step = 0; step <= E.t.n; step++) {
        if (dir > 0) {
            int from = step == 0 ? x + 1 : 0;
            for (int i = from; i + pl <= lenof(y); i++) if (match_at(y, i, p, pl)) { *oy = y; *ox = i; return 1; }
            if (++y >= E.t.n) { y = 0; *wrapped = 1; }
        } else {
            int from = step == 0 ? x - 1 : lenof(y) - pl;
            for (int i = from; i >= 0; i--) if (match_at(y, i, p, pl)) { *oy = y; *ox = i; return 1; }
            if (--y < 0) { y = E.t.n - 1; *wrapped = 1; }
        }
    }
    return 0;                                        /* the last step came back to the cursor's line, all of it */
}
static void search(int dir, const char *p) {
    int y, x, w;
    if (!find(dir, p, &y, &x, &w)) { msg("E486: Pattern not found: ", p, 0); return; }
    E.cy = y; E.cx = x; E.want = x;
    if (w) msg(dir > 0 ? "search hit BOTTOM, continuing at TOP" : "search hit TOP, continuing at BOTTOM", 0, 0);
    else { msg(dir > 0 ? "/" : "?", p, 0); }
}
/* :s/PAT/REP/[g] on line a..b; the whole job one undo step. */
static void substitute(int a, int b, char *spec) {
    char *pat = spec, *rep, *fl;
    rep = pat; while (*rep && *rep != '/') rep++;
    if (!*rep) { msg("E486: Pattern not found: ", pat, 0); return; }
    *rep++ = 0;
    fl = rep; while (*fl && *fl != '/') fl++;
    if (*fl) *fl++ = 0;
    int all = 0; for (char *f = fl; *f; f++) if (*f == 'g') all = 1;
    int pl = 0; while (pat[pl]) pl++;
    int rl = 0; while (rep[rl]) rl++;
    if (!pl) { if (!last_pat[0]) { msg("E35: No previous regular expression", 0, 0); return; } pat = last_pat; pl = 0; while (pat[pl]) pl++; }
    else scopy(last_pat, pat, sizeof(last_pat));
    int subs = 0, lines = 0, last = -1, begun = 0;
    for (int y = a; y <= b; y++) {
        int did = 0;
        for (int x = 0; x + pl <= lenof(y); ) {
            if (!match_at(y, x, pat, pl)) { x++; continue; }
            if (lenof(y) - pl + rl > T_COLS) break;       /* it would not fit */
            if (!begun) { change_begin(); begun = 1; }
            for (int i = 0; i < pl; i++) text_delete_char(&E.t, y, x);
            for (int i = 0; i < rl; i++) text_insert_char(&E.t, y, x + i, rep[i]);
            x += rl; subs++; did = 1;
            if (!all) break;
        }
        if (did) { lines++; last = y; }
    }
    if (!subs) { msg("E486: Pattern not found: ", pat, 0); return; }
    E.cy = last; E.cx = first_nonblank(&E.t, last); E.want = E.cx;
    char d[12]; int n = 11; d[n] = 0; unsigned v = (unsigned)subs;
    do { d[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    msg(d + n, subs == 1 ? " substitution on " : " substitutions on ", 0);
    int l = 0; while (E.msg[l]) l++;
    n = 11; v = (unsigned)lines; do { d[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    for (const char *q = d + n; *q && l < 70; q++) E.msg[l++] = *q;
    for (const char *q = lines == 1 ? " line" : " lines"; *q && l < 79; q++) E.msg[l++] = *q;
    E.msg[l] = 0;
}

/* ---- files and commands ---- */
static void open_file(const char *path) {
    scopy(E.path, path, sizeof(E.path));
    int r = text_load(&E.t, path);
    E.cy = E.cx = E.want = E.top = E.left = 0; E.changed = 0;
    undo_reset(); E.seq = E.saved = E.idgen = 0;
    if (r == -1) { eprint("[neosim] new "); eprint(path); eprint("\n"); msg("\"", path, "\" [New]"); return; }
    eprint("[neosim] open "); eprint(path); eprint(" lines="); eprint_u((unsigned)E.t.n); eprint("\n");
    if (r == -2) msg("\"", path, "\" too long: only the first lines");
    else if (E.t.cut) msg("\"", path, "\" (long lines cut at 255)");
    else msg_num("\"", path, (unsigned)E.t.n, "L");
}
static int write_file(const char *path) {
    unsigned bytes = 0;
    if (!path[0]) { msg("E32: No file name", 0, 0); return -1; }
    if (text_save(&E.t, path, &bytes) < 0) { msg("E212: Can't open file for writing: ", path, 0); eprint("[neosim] write failed\n"); return -1; }
    eprint("[neosim] wrote "); eprint(path); eprint(" bytes="); eprint_u(bytes); eprint("\n");
    if (path != E.path) scopy(E.path, path, sizeof(E.path));
    E.saved = E.seq; E.changed = 0;
    msg_num("\"", path, bytes, "B written");
    return 0;
}
static int command(char *c) {                        /* 1: end neosim */
    while (*c == ' ') c++;
    if ((c[0] == 's' && c[1] == '/') || (c[0] == '%' && c[1] == 's' && c[2] == '/')) {   /* :s keeps its spaces */
        if (c[0] == 's') substitute(E.cy, E.cy, c + 2); else substitute(0, E.t.n - 1, c + 3);
        return 0;
    }
    char *arg = c;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    if (same(c, "q")) {
        if (E.changed) { msg("E37: No write since last change (add ! to override)", 0, 0); eprint("[neosim] refused :q (changed)\n"); return 0; }
        return 1;
    }
    if (same(c, "q!")) return 1;
    if (same(c, "w")) { write_file(*arg ? arg : E.path); return 0; }
    if (same(c, "wq") || same(c, "x")) {
        if (same(c, "x") && !E.changed) return 1;
        return write_file(*arg ? arg : E.path) == 0;
    }
    if (same(c, "e") || same(c, "e!")) {
        if (!*arg) { msg("E32: No file name", 0, 0); return 0; }
        if (E.changed && same(c, "e")) { msg("E37: No write since last change (add ! to override)", 0, 0); return 0; }
        open_file(arg);
        return 0;
    }
    if (c[0] == 's' && c[1] == '/') { substitute(E.cy, E.cy, c + 2); return 0; }
    if (c[0] == '%' && c[1] == 's' && c[2] == '/') { substitute(0, E.t.n - 1, c + 3); return 0; }
    if (!*c) return 0;
    msg("E492: Not an editor command: ", c, 0);
    return 0;
}

/* ---- auto-indent (v0.60.218): as vim's autoindent ---- */
static int ai_pending;                               /* spaces put at this line's start, nothing typed after */
static int indent_of(int y) { int i = 0; while (i < lenof(y) && E.t.s[y][i] == ' ') i++; return i; }
static int only_spaces(int y) { return indent_of(y) == lenof(y); }
static void put_indent(int y, int n) { for (int i = 0; i < n; i++) text_insert_char(&E.t, y, 0, ' '); }

/* ---- insert mode ---- */
static void insert_mode(int x, int push) {
    if (push) change_begin();
    E.ins_dirty = !push;                            /* an operator before it already changed the text */
    E.mode = M_INSERT; E.cx = x; clamp(); E.want = E.cx;
}
static void insert_key(unsigned k) {
    if (k == 27) {
        if (ai_pending && only_spaces(E.cy)) { while (lenof(E.cy)) text_delete_char(&E.t, E.cy, 0); E.cx = 0; }   /* an indent nothing came after */
        ai_pending = 0;
        E.mode = M_NORMAL;
        if (!E.ins_dirty) { undo_discard(&E.seq); E.changed = E.seq != E.saved; }   /* nothing typed: no step */
        E.cx--; clamp(); E.want = E.cx;
        return;
    }
    if (k == KEY_LEFT) { E.cx--; clamp(); E.want = E.cx; return; }
    if (k == KEY_RIGHT) { E.cx++; clamp(); E.want = E.cx; return; }
    if (k == KEY_UP || k == KEY_DOWN) { E.cy += k == KEY_UP ? -1 : 1; E.cx = E.want; clamp(); return; }
    if (k == '\n') {                                /* the new line keeps this one's indent (v0.60.218) */
        int ind = indent_of(E.cy);
        if (ai_pending && only_spaces(E.cy)) { while (lenof(E.cy)) text_delete_char(&E.t, E.cy, 0); E.cx = 0; }
        if (text_split(&E.t, E.cy, E.cx) == 0) {
            E.cy++; put_indent(E.cy, ind); E.cx = E.want = ind; E.ins_dirty = 1; ai_pending = ind;
        }
        return;
    }
    if (k == '\b') {
        if (E.cx > 0) { text_delete_char(&E.t, E.cy, E.cx - 1); E.cx--; E.want = E.cx; E.ins_dirty = 1; }
        else if (E.cy > 0) { int at = lenof(E.cy - 1); text_join(&E.t, E.cy - 1); E.cy--; E.cx = E.want = at; E.ins_dirty = 1; }
        return;
    }
    if (k == '\t') { for (int i = 0; i < 4; i++) if (text_insert_char(&E.t, E.cy, E.cx, ' ') == 0) E.cx++; E.want = E.cx; E.ins_dirty = 1; return; }
    if (k >= ' ' && k < 127) {
        ai_pending = 0;
        if (text_insert_char(&E.t, E.cy, E.cx, (char)k) == 0) { E.cx++; E.want = E.cx; E.ins_dirty = 1; }
        else msg("the line is full (255)", 0, 0);
    }
}

/* ---- motions ---- */
typedef struct { int ok, y, x, line, incl; } mv_t;
static mv_t motion(unsigned k, int cnt, int had, int op) {
    mv_t m = { 1, E.cy, E.cx, 0, 0 };
    switch (k) {
    case 'h': case KEY_LEFT: m.x = imax(E.cx - cnt, 0); m.ok = m.x != E.cx; break;
    case 'l': case KEY_RIGHT: m.x = imin(E.cx + cnt, op ? len() : imax(len() - 1, 0)); m.ok = m.x != E.cx; break;
    case 'j': case KEY_DOWN: m.y = imin(E.cy + cnt, E.t.n - 1); m.line = 1; m.ok = m.y != E.cy; break;
    case 'k': case KEY_UP: m.y = imax(E.cy - cnt, 0); m.line = 1; m.ok = m.y != E.cy; break;
    case '0': m.x = 0; break;
    case '^': m.x = first_nonblank(&E.t, E.cy); break;
    case '$': m.y = imin(E.cy + cnt - 1, E.t.n - 1); m.x = lenof(m.y) ? lenof(m.y) - 1 : 0; m.incl = 1;
              if (op && !lenof(m.y)) m.ok = 0; break;
    case 'w': {
        int y = E.cy, x = E.cx, py = y;
        for (int i = 0; i < cnt; i++) { py = y; word_fwd(&E.t, &y, &x, op); }
        if (op && y > py) { y = py; x = lenof(py); }  /* an operator's w stops at the end of the line */
        m.y = y; m.x = x; break;
    }
    case 'b': for (int i = 0; i < cnt; i++) word_back(&E.t, &m.y, &m.x); break;
    case 'e': for (int i = 0; i < cnt; i++) word_end(&E.t, &m.y, &m.x); m.incl = 1; break;
    case '%': {                                     /* (v0.60.218) the bracket at or after the cursor, to its match */
        static const char open_[] = "([{", close_[] = ")]}";
        int x = E.cx, y = E.cy;
        while (x < lenof(y)) { char c = E.t.s[y][x]; int k2 = 0; while (k2 < 3 && c != open_[k2] && c != close_[k2]) k2++; if (k2 < 3) break; x++; }
        if (x >= lenof(y)) { m.ok = 0; break; }
        char c = E.t.s[y][x];
        int k2 = 0; while (c != open_[k2] && c != close_[k2]) k2++;
        char o = open_[k2], cl = close_[k2];
        int fwd = c == o, depth = 0, found = 0;
        while (y >= 0 && y < E.t.n) {
            if (x >= 0 && x < lenof(y)) {
                char d = E.t.s[y][x];
                if (d == o) depth += fwd ? 1 : -1;
                else if (d == cl) depth += fwd ? -1 : 1;
                if (depth == 0) { found = 1; break; }
            }
            if (fwd) { if (++x >= lenof(y)) { y++; x = -1; } }
            else { if (--x < 0) { y--; x = y >= 0 ? lenof(y) : 0; } }
        }
        if (!found) { m.ok = 0; break; }
        m.y = y; m.x = x; m.incl = 1;
        break;
    }
    case 'G': m.y = had ? imin(imax(cnt - 1, 0), E.t.n - 1) : E.t.n - 1; m.x = E.want; m.line = 1; break;
    case 'g' + 256: m.y = had ? imin(imax(cnt - 1, 0), E.t.n - 1) : 0; m.x = E.want; m.line = 1; break;   /* gg */
    default: m.ok = 0;
    }
    return m;
}

/* ---- operators ---- */
static void shift_lines(int a, int b, int dir);
static void op_lines(int op, int a, int b) {
    if (op == '>' || op == '<') { shift_lines(a, b, op == '>' ? 1 : -1); return; }
    a = imax(a, 0); b = imin(b, E.t.n - 1);
    reg_take_lines(a, b);
    if (op == 'y') { E.cy = a; clamp(); return; }
    change_begin();
    if (op == 'd') { delete_lines(a, b); E.cy = imin(a, E.t.n - 1); E.cx = first_nonblank(&E.t, E.cy); E.want = E.cx; return; }
    delete_lines(a, b);                              /* c: one empty line where they were */
    if (E.t.n == 1 && lenof(0) == 0 && a == 0) { E.cy = 0; insert_mode(0, 0); return; }
    text_insert_line(&E.t, a); E.cy = a; insert_mode(0, 0);
}
static void shift_lines(int a, int b, int dir) {      /* > and <: four spaces, empty lines left alone (v0.60.206) */
    a = imax(a, 0); b = imin(b, E.t.n - 1);
    change_begin();
    for (int y = a; y <= b; y++) {
        if (dir > 0) { if (lenof(y)) for (int i = 0; i < 4; i++) text_insert_char(&E.t, y, 0, ' '); }
        else for (int i = 0; i < 4 && lenof(y) && E.t.s[y][0] == ' '; i++) text_delete_char(&E.t, y, 0);
    }
    E.cy = a; E.cx = first_nonblank(&E.t, a); E.want = E.cx;
}
static void op_apply(int op, mv_t m) {
    if (op == '>' || op == '<') { shift_lines(imin(E.cy, m.y), imax(E.cy, m.y), op == '>' ? 1 : -1); return; }
    if (m.line) { op_lines(op, imin(E.cy, m.y), imax(E.cy, m.y)); return; }
    int ay = E.cy, ax = E.cx, by = m.y, bx = m.x;
    if (by < ay || (by == ay && bx < ax)) { int t = ay; ay = by; by = t; t = ax; ax = bx; bx = t; }
    if (m.incl) bx++;
    if (by == ay) bx = imin(bx, lenof(ay));
    reg_take_chars(ay, ax, by, bx);
    if (op == 'y') { E.cy = ay; E.cx = ax; clamp(); E.want = E.cx; return; }
    change_begin();
    delete_chars(ay, ax, by, bx);
    E.cy = ay; E.cx = ax;
    if (op == 'c') { insert_mode(ax, 0); return; }
    clamp(); E.want = E.cx;
}
static void paste(int after, int cnt) {
    if (!reg_n && !reg_lines) return;
    change_begin();
    if (reg_lines) {
        int y = after ? E.cy + 1 : E.cy, at = y;
        for (int c = 0; c < cnt; c++) {
            if (text_insert_line(&E.t, y) < 0) break;
            int yy = y, xx = 0;
            insert_text(&yy, &xx, reg, reg_n);
            y = yy + 1;
        }
        E.cy = at; E.cx = first_nonblank(&E.t, at); E.want = E.cx;
        return;
    }
    int y = E.cy, x = after && len() ? E.cx + 1 : E.cx;
    for (int c = 0; c < cnt; c++) insert_text(&y, &x, reg, reg_n);
    E.cy = y; E.cx = x - 1; clamp(); E.want = E.cx;
}
static void join(int cnt) {
    int times = imax(cnt, 2) - 1;
    if (E.cy + 1 >= E.t.n) return;
    change_begin();
    for (int i = 0; i < times && E.cy + 1 < E.t.n; i++) {
        int n2 = E.cy + 1, skip = 0;
        while (skip < lenof(n2) && E.t.s[n2][skip] == ' ') skip++;
        int at = len();
        int space = at > 0 && skip < lenof(n2) && E.t.s[E.cy][at - 1] != ' ' && E.t.s[n2][skip] != ')';
        for (int k = 0; k < skip; k++) text_delete_char(&E.t, n2, 0);
        if (space) text_insert_char(&E.t, E.cy, at, ' ');
        text_join(&E.t, E.cy);
        E.cx = space ? at : (at ? at - 1 : 0);
        if (!space && at < len()) E.cx = at;
    }
    clamp(); E.want = E.cx;
}

/* ---- normal mode: a count, an operator waiting, a g waiting ---- */
static struct { int count, op, opcount, ophad, g, gcount, ghad; } P;
static void normal_key(unsigned k) {
    if (k == 27) { P.count = P.op = P.g = 0; return; }
    if ((k >= '1' && k <= '9') || (k == '0' && P.count)) { P.count = P.count * 10 + (int)(k - '0'); if (P.count > 99999) P.count = 99999; return; }
    int had = P.count > 0, cnt = had ? P.count : 1;
    P.count = 0;
    if (P.g) {                                      /* g then: only gg */
        P.g = 0;
        if (k != 'g') { P.op = 0; return; }
        k = 'g' + 256; cnt = P.gcount; had = P.ghad;
    } else if (k == 'g') { P.g = 1; P.gcount = cnt; P.ghad = had; return; }
    if (P.op) {                                     /* an operator waiting for its motion */
        int op = P.op, total = cnt * P.opcount, thad = had || P.ophad;
        P.op = 0;
        if ((int)k == op) { op_lines(op, E.cy, E.cy + total - 1); return; }
        if (op == 'c' && k == 'w' && len() && E.t.s[E.cy][E.cx] != ' ') k = 'e';   /* cw is ce on a word */
        mv_t m = motion(k, total, thad, 1);
        if (m.ok) op_apply(op, m);
        return;
    }
    mv_t m = motion(k, cnt, had, 0);
    if (m.ok) {
        int vertical = m.line;
        E.cy = m.y;
        if (vertical) { E.cx = E.want; clamp(); }
        else { E.cx = m.x; clamp(); E.want = (k == '$') ? 9999 : E.cx; }
        return;
    }
    if (m.ok == 0 && (k == 'h' || k == 'l' || k == 'j' || k == 'k' || k >= 256)) return;
    switch (k) {
    case 'd': case 'c': case 'y': case '>': case '<': P.op = (int)k; P.opcount = cnt; P.ophad = had; break;
    case 'n': for (int i = 0; i < cnt; i++) search(last_dir, last_pat); break;
    case 'N': for (int i = 0; i < cnt; i++) search(-last_dir, last_pat); break;
    case '/': case '?': E.mode = M_COMMAND; E.cmdch = (char)k; E.cmdn = 0; break;
    case 'v': E.mode = M_VISUAL; E.vy = E.cy; E.vx = E.cx; break;
    case 'V': E.mode = M_VLINE; E.vy = E.cy; E.vx = E.cx; break;
    case 'D': { mv_t e = motion('$', cnt, had, 1); if (e.ok) op_apply('d', e); break; }
    case 'C': { mv_t e = motion('$', cnt, had, 1); if (e.ok) op_apply('c', e); else insert_mode(E.cx, 1); break; }
    case 'x': if (len()) { mv_t e = { 1, E.cy, imin(E.cx + cnt, len()), 0, 0 }; op_apply('d', e); } break;
    case 'X': if (E.cx) { mv_t e = { 1, E.cy, imax(E.cx - cnt, 0), 0, 0 }; op_apply('d', e); } break;
    case 'p': paste(1, cnt); break;
    case 'P': paste(0, cnt); break;
    case 'J': join(had ? cnt : 2); break;
    case 'u': { int n = 0; for (int i = 0; i < cnt; i++) n += undo_undo(&E.t, &E.cy, &E.cx, &E.seq); after_undo(); if (!n) msg("Already at oldest change", 0, 0); break; }
    case 0x12: { int n = 0; for (int i = 0; i < cnt; i++) n += undo_redo(&E.t, &E.cy, &E.cx, &E.seq); after_undo(); if (!n) msg("Already at newest change", 0, 0); break; }
    case 'i': insert_mode(E.cx, 1); break;
    case 'a': insert_mode(len() ? E.cx + 1 : 0, 1); break;
    case 'A': insert_mode(len(), 1); break;
    case 'I': insert_mode(len() ? first_nonblank(&E.t, E.cy) : 0, 1); break;
    case 'o': { int ind = indent_of(E.cy); change_begin(); if (text_insert_line(&E.t, E.cy + 1) == 0) { E.cy++; put_indent(E.cy, ind); insert_mode(ind, 0); ai_pending = ind; } break; }
    case 'O': { int ind = indent_of(E.cy); change_begin(); if (text_insert_line(&E.t, E.cy) == 0) { put_indent(E.cy, ind); insert_mode(ind, 0); ai_pending = ind; } break; }
    case ':': E.mode = M_COMMAND; E.cmdch = ':'; E.cmdn = 0; break;
    }
}
/* ---- visual mode (v0.60.206): the motions move the cursor, an operator
 * takes from where v or V was pressed to it ---- */
static char rec[256], cur[256];                     /* . : the last change's keys, and this command's */
static int recn, curn, cur_change, replaying;
static void visual_key(unsigned k) {
    if (k == 27) { E.mode = M_NORMAL; P.count = 0; return; }
    if ((k >= '1' && k <= '9') || (k == '0' && P.count)) { P.count = P.count * 10 + (int)(k - '0'); return; }
    int had = P.count > 0, cnt = had ? P.count : 1;
    P.count = 0;
    if (P.g) { P.g = 0; if (k == 'g') { mv_t m = motion('g' + 256, cnt, had, 0); E.cy = m.y; E.cx = E.want; clamp(); } return; }
    if (k == 'g') { P.g = 1; return; }
    if (k == 'v' || k == 'V') { int want = k == 'v' ? M_VISUAL : M_VLINE; E.mode = E.mode == want ? M_NORMAL : want; return; }
    int op = k == 'x' ? 'd' : (int)k;
    if (op == 'd' || op == 'y' || op == 'c' || op == '>' || op == '<') {
        int line = E.mode == M_VLINE, dy = E.cy - E.vy, dx = E.cx - E.vx;
        if (dy < 0) dy = -dy;
        /* What . does again: as many lines (or characters on one line) from the cursor. */
        curn = 0; cur_change = op != 'y';
        cur[curn++] = line || op == '>' || op == '<' ? 'V' : 'v';
        for (int i = 0; i < dy && curn < 200; i++) cur[curn++] = 'j';
        if (!line && dy == 0) { if (dx < 0) dx = -dx; for (int i = 0; i < dx && curn < 200; i++) cur[curn++] = 'l'; }
        cur[curn++] = (char)op;
        mv_t m = { 1, E.cy, E.cx, line || op == '>' || op == '<', 1 };
        E.mode = M_NORMAL;
        int cy = E.cy, cx = E.cx;
        E.cy = E.vy; E.cx = E.vx;
        m.y = cy; m.x = cx;
        if (!m.line && cx >= lenof(cy) && lenof(cy) > 0) m.x = lenof(cy) - 1;
        op_apply(op, m);
        return;
    }
    mv_t m = motion(k, cnt, had, 0);
    if (!m.ok) return;
    E.cy = m.y;
    if (m.line) { E.cx = E.want; clamp(); } else { E.cx = m.x; clamp(); E.want = k == '$' ? 9999 : E.cx; }
}
static int key(unsigned k);
static int key_inner(unsigned k);
static void dot(void) {                             /* the last change again */
    if (!recn || replaying) return;
    char again[256]; int n = recn;
    for (int i = 0; i < n; i++) again[i] = rec[i];
    replaying = 1;
    for (int i = 0; i < n; i++) key((unsigned char)again[i] == 27 ? 27u : (unsigned)(unsigned char)again[i]);
    replaying = 0;
}
static int idle(void) { return E.mode == M_NORMAL && !P.count && !P.op && !P.g; }
static int key(unsigned k) {                        /* 1: end neosim */
    if (!replaying) {                               /* the keys of a change, for . */
        if (idle() && k == '.') { E.msg[0] = 0; dot(); return 0; }
        if (idle()) { curn = 0; cur_change = 0; }
        if (E.mode == M_NORMAL || E.mode == M_INSERT) { if (curn < (int)sizeof(cur)) cur[curn++] = k < 256 ? (char)k : 0; else cur_change = -1; }
        if (k >= 256) cur_change = -1;              /* arrows are not kept */
    }
    cur_change_hook = 0;
    int r = key_inner(k);
    if (!replaying && cur_change_hook && cur_change == 0) cur_change = 1;
    if (!replaying && idle() && cur_change == 1) { for (int i = 0; i < curn; i++) rec[i] = cur[i]; recn = curn; cur_change = 0; }
    return r;
}
static int key_inner(unsigned k) {
    if (E.mode == M_COMMAND) {
        if (k == 27) { E.mode = M_NORMAL; cur_change = -1; return 0; }
        if (k == '\n') {
            E.cmd[E.cmdn] = 0; E.mode = M_NORMAL; E.msg[0] = 0; cur_change = -1;
            if (E.cmdch == '/' || E.cmdch == '?') {
                last_dir = E.cmdch == '/' ? 1 : -1;
                if (E.cmdn) scopy(last_pat, E.cmd, sizeof(last_pat));
                if (!last_pat[0]) { msg("E35: No previous regular expression", 0, 0); return 0; }
                search(last_dir, last_pat);
                return 0;
            }
            return command(E.cmd);
        }
        if (k == '\b') { if (E.cmdn) E.cmdn--; else E.mode = M_NORMAL; return 0; }
        if (k >= ' ' && k < 127 && E.cmdn < (int)sizeof(E.cmd) - 1) E.cmd[E.cmdn++] = (char)k;
        return 0;
    }
    E.msg[0] = 0;
    if (E.mode == M_INSERT) insert_key(k);
    else if (E.mode == M_VISUAL || E.mode == M_VLINE) visual_key(k);
    else normal_key(k);
    return 0;
}

int main(void) {
    char *argv[8];
    int argc = uargv(argv, 8), wait = 0;
    const char *file = 0;
    for (int i = 0; i < argc; i++) {
        if (same(argv[i], "-wait")) wait = 1;
        else if (!file) file = argv[i];
    }
    if (text_init() < 0 || undo_init() < 0) { print("neosim: no memory\n"); return 1; }
    E.t.n = 0;
    text_clear(&E.t);
    if (file) open_file(file);
    int win = sys_win_open(640, 480, "neosim");
    for (int i = 0; wait && win < 0 && i < 600; i++) { sys_sleep(100); win = sys_win_open(640, 480, "neosim"); }
    if (win < 0) { print("neosim could not open its window (is the countertop up?)\n"); return 1; }
    unsigned *body = (unsigned *)sys_sbrk(640 * 480 * 4);
    if (body == (unsigned *)-1) { print("neosim: no memory for the screen\n"); return 1; }
    eprint("[neosim] window up\n");
    int done = 0;
    while (!done) {
        scroll();
        screen_draw(&E, body);
        sys_win_put(win, body);
        uwinev_t ev;
        int g = sys_win_event(win, &ev, 1000);
        if (g < 0) break;
        while (g == 1 && !done) {
            if (ev.type == WEV_KEY) done = key(ev.key);
            else if (ev.type == WEV_CLOSE) done = command((char[]){ 'q', 0 });
            g = sys_win_event(win, &ev, 0);
        }
    }
    eprint("[neosim] bye\n");
    sys_win_close(win);
    return 0;
}

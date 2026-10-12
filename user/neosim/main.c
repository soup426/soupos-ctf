/* main.c - neosim (v0.60.204): a modal editor after neovim, in a window of
 * its own on the desktop. The user's name for it: it simmers.
 *
 *   cook neosim.elf FILE          (from a terminal on the desktop)
 *   cook neosim.elf -wait FILE &  wait up to a minute for the desktop, for
 *                                 tests that start it at the console
 *
 * Normal mode: counts; h j k l and the arrows, w b e 0 ^ $ gg G (v0.60.205);
 * the operators d c y with a motion, dd cc yy, D C; x X, p P, J; u and
 * Ctrl-R; i a A I o O into insert mode; : for a command. Insert mode:
 * typing, Enter, Backspace (joining lines at the start of one), Tab as four
 * spaces, the arrows, Esc back. Commands: :w [FILE], :q, :q!, :wq, :x,
 * :e[!] FILE. :q with changes is refused, as vim's E37. The close box is :q.
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
static void change_begin(void) {                    /* before every change: a step back to this */
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
    if (!*c) return 0;
    msg("E492: Not an editor command: ", c, 0);
    return 0;
}

/* ---- insert mode ---- */
static void insert_mode(int x, int push) {
    if (push) change_begin();
    E.ins_dirty = !push;                            /* an operator before it already changed the text */
    E.mode = M_INSERT; E.cx = x; clamp(); E.want = E.cx;
}
static void insert_key(unsigned k) {
    if (k == 27) {
        E.mode = M_NORMAL;
        if (!E.ins_dirty) { undo_discard(&E.seq); E.changed = E.seq != E.saved; }   /* nothing typed: no step */
        E.cx--; clamp(); E.want = E.cx;
        return;
    }
    if (k == KEY_LEFT) { E.cx--; clamp(); E.want = E.cx; return; }
    if (k == KEY_RIGHT) { E.cx++; clamp(); E.want = E.cx; return; }
    if (k == KEY_UP || k == KEY_DOWN) { E.cy += k == KEY_UP ? -1 : 1; E.cx = E.want; clamp(); return; }
    if (k == '\n') { if (text_split(&E.t, E.cy, E.cx) == 0) { E.cy++; E.cx = E.want = 0; E.ins_dirty = 1; } return; }
    if (k == '\b') {
        if (E.cx > 0) { text_delete_char(&E.t, E.cy, E.cx - 1); E.cx--; E.want = E.cx; E.ins_dirty = 1; }
        else if (E.cy > 0) { int at = lenof(E.cy - 1); text_join(&E.t, E.cy - 1); E.cy--; E.cx = E.want = at; E.ins_dirty = 1; }
        return;
    }
    if (k == '\t') { for (int i = 0; i < 4; i++) if (text_insert_char(&E.t, E.cy, E.cx, ' ') == 0) E.cx++; E.want = E.cx; E.ins_dirty = 1; return; }
    if (k >= ' ' && k < 127) {
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
    case 'G': m.y = had ? imin(imax(cnt - 1, 0), E.t.n - 1) : E.t.n - 1; m.x = E.want; m.line = 1; break;
    case 'g' + 256: m.y = had ? imin(imax(cnt - 1, 0), E.t.n - 1) : 0; m.x = E.want; m.line = 1; break;   /* gg */
    default: m.ok = 0;
    }
    return m;
}

/* ---- operators ---- */
static void op_lines(int op, int a, int b) {
    a = imax(a, 0); b = imin(b, E.t.n - 1);
    reg_take_lines(a, b);
    if (op == 'y') { E.cy = a; clamp(); return; }
    change_begin();
    if (op == 'd') { delete_lines(a, b); E.cy = imin(a, E.t.n - 1); E.cx = first_nonblank(&E.t, E.cy); E.want = E.cx; return; }
    delete_lines(a, b);                              /* c: one empty line where they were */
    if (E.t.n == 1 && lenof(0) == 0 && a == 0) { E.cy = 0; insert_mode(0, 0); return; }
    text_insert_line(&E.t, a); E.cy = a; insert_mode(0, 0);
}
static void op_apply(int op, mv_t m) {
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
    case 'd': case 'c': case 'y': P.op = (int)k; P.opcount = cnt; P.ophad = had; break;
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
    case 'o': change_begin(); if (text_insert_line(&E.t, E.cy + 1) == 0) { E.cy++; insert_mode(0, 0); } break;
    case 'O': change_begin(); if (text_insert_line(&E.t, E.cy) == 0) insert_mode(0, 0); break;
    case ':': E.mode = M_COMMAND; E.cmdn = 0; break;
    }
}
static int key(unsigned k) {                        /* 1: end neosim */
    if (E.mode == M_COMMAND) {
        if (k == 27) { E.mode = M_NORMAL; return 0; }
        if (k == '\n') { E.cmd[E.cmdn] = 0; E.mode = M_NORMAL; E.msg[0] = 0; return command(E.cmd); }
        if (k == '\b') { if (E.cmdn) E.cmdn--; else E.mode = M_NORMAL; return 0; }
        if (k >= ' ' && k < 127 && E.cmdn < (int)sizeof(E.cmd) - 1) E.cmd[E.cmdn++] = (char)k;
        return 0;
    }
    E.msg[0] = 0;
    if (E.mode == M_INSERT) insert_key(k); else normal_key(k);
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

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
 * % to the matching bracket; Enter, o and O keep the indent (v0.60.218);
 * :help, a read-only page of all this (v0.60.219); f t F T ; , r ~ (v0.60.222);
 * Ctrl-D Ctrl-U Ctrl-F Ctrl-B, zz zt zb (v0.60.223); * # (v0.60.226);
 * text objects iw aw i" a" i( a( i{ a{ ... (v0.60.227); marks m ' ` (v0.60.231);
 * gu gU g~ and in visual u U ~ (v0.60.232); counts on inserts, 3. (v0.60.234)
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
#include "help.h"
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
static struct { int y, x, set; } marks[26];      /* (v0.60.231) m a .. m z */
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
static int last_whole;                               /* (v0.60.226) the last search was * or #: whole words */
static int kw(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (unsigned char)c >= 192; }
static int match_at(int y, int x, const char *p, int pl) {
    if (x + pl > lenof(y)) return 0;
    for (int i = 0; i < pl; i++) if (E.t.s[y][x + i] != p[i]) return 0;
    return 1;
}
static int search_whole;                             /* find() wants whole words */
static int match_find(int y, int x, const char *p, int pl) {
    if (!match_at(y, x, p, pl)) return 0;
    if (!search_whole) return 1;
    return (x == 0 || !kw(E.t.s[y][x - 1])) && (x + pl >= lenof(y) || !kw(E.t.s[y][x + pl]));
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
            for (int i = from; i + pl <= lenof(y); i++) if (match_find(y, i, p, pl)) { *oy = y; *ox = i; return 1; }
            if (++y >= E.t.n) { y = 0; *wrapped = 1; }
        } else {
            int from = step == 0 ? x - 1 : lenof(y) - pl;
            for (int i = from; i >= 0; i--) if (match_find(y, i, p, pl)) { *oy = y; *ox = i; return 1; }
            if (--y < 0) { y = E.t.n - 1; *wrapped = 1; }
        }
    }
    return 0;                                        /* the last step came back to the cursor's line, all of it */
}
static void search(int dir, const char *p) {
    int y, x, w;
    search_whole = last_whole;
    int got = find(dir, p, &y, &x, &w);
    search_whole = 0;
    if (!got) { msg("E486: Pattern not found: ", p, 0); return; }
    E.cy = y; E.cx = x; E.want = x;
    if (w) msg(dir > 0 ? "search hit BOTTOM, continuing at TOP" : "search hit TOP, continuing at BOTTOM", 0, 0);
    else if (last_whole) msg(dir > 0 ? "/\\<" : "?\\<", p, "\\>");
    else { msg(dir > 0 ? "/" : "?", p, 0); }
}
/* * and # (v0.60.226): the keyword under the cursor, or the next on the
 * line, as a whole word, from that word's own start, forward or back. */
static void star(int dir, int cnt) {
    int n = lenof(E.cy), x = E.cx;
    const char *t = E.t.s[E.cy];
    while (x < n && !kw(t[x])) x++;
    if (x >= n) { msg("E348: No string under cursor", 0, 0); return; }
    while (x > 0 && kw(t[x - 1])) x--;
    int e = x, k = 0;
    while (e < n && kw(t[e]) && k < (int)sizeof(last_pat) - 1) last_pat[k++] = t[e++];
    last_pat[k] = 0;
    last_dir = dir; last_whole = 1;
    E.cx = x;
    for (int i = 0; i < cnt; i++) search(dir, last_pat);
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
    for (int i = 0; i < 26; i++) marks[i].set = 0;   /* (v0.60.231) a file's marks are its own */
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
static const char E21[] = "E21: Cannot make changes, 'modifiable' is off";
static int command(char *c) {                        /* 1: end neosim */
    while (*c == ' ') c++;
    if (E.help) {                                    /* (v0.60.219) on the help page: only leaving it, or :e */
        if (same(c, "q") || same(c, "q!") || same(c, "close")) { E.help = 0; return 0; }
        if (same(c, "help") || same(c, "h")) return 0;
        if (c[0] == 'w' || same(c, "x")) { msg("E382: Cannot write, 'buftype' option is set", 0, 0); return 0; }
        if (!(c[0] == 'e' && (c[1] == ' ' || c[1] == '!'))) { msg(E21, 0, 0); return 0; }
        E.help = 0;
    }
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
    if (same(c, "help") || same(c, "h")) { E.help = 1; E.htop = 0; eprint("[neosim] help\n"); return 0; }
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
/* A count on i a I A o O (v0.60.234): what is typed is typed again at Esc,
 * count - 1 times (for o and O after a new line each time); one undo step. */
static int ins_count = 1, ins_open, ins_n, ins_again;
static unsigned ins_keys[256];
static void insert_mode(int x, int push) {
    ins_count = 1; ins_open = 0; ins_n = 0;
    if (push) change_begin();
    E.ins_dirty = !push;                            /* an operator before it already changed the text */
    E.mode = M_INSERT; E.cx = x; clamp(); E.want = E.cx;
}
static void insert_key(unsigned k) {
    if (k == 27 && ins_count > 1 && ins_n && !ins_again) {
        ins_again = 1;
        for (int r = 1; r < ins_count; r++) {
            if (ins_open) insert_key('\n');
            for (int i = 0; i < ins_n; i++) insert_key(ins_keys[i]);
        }
        ins_again = 0;
    }
    if (k != 27 && !ins_again && ins_n < (int)(sizeof ins_keys / sizeof ins_keys[0])) ins_keys[ins_n++] = k;
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
/* Marks (v0.60.231): m and a letter; ' to its line's first non-blank, `
 * to its place. They move with lines put in or taken out above them; a
 * mark on a line taken out is gone, on a line joined it goes along. */
static void marks_lines(int at, int delta, int join_col) {
    for (int i = 0; i < 26; i++) {
        if (!marks[i].set) continue;
        if (delta > 0) { if (marks[i].y >= at) marks[i].y++; }
        else if (marks[i].y == at) { if (join_col >= 0) { marks[i].y = at - 1; marks[i].x += join_col; } else marks[i].set = 0; }
        else if (marks[i].y > at) marks[i].y--;
    }
}
/* f t F T and a character, ; and , (v0.60.222): on the cursor's line. */
static int find_ch, last_find, last_find_ch;         /* the character just typed; the last f t F T and its */
static int find_on_line(int cmd, int ch, int cnt, int again) {   /* the column it lands on, or -1 */
    int fwd = cmd == 'f' || cmd == 't', x = E.cx;
    const char *t = E.t.s[E.cy]; int n = len();
    if (fwd) {
        int at = x + 1;
        if (again && cmd == 't' && at < n && t[at] == ch) at++;   /* ; after t: past the one it stands before */
        for (; at < n; at++) if (t[at] == ch && --cnt == 0) return cmd == 't' ? at - 1 : at;
    } else {
        int at = x - 1;
        if (again && cmd == 'T' && at >= 0 && t[at] == ch) at--;
        for (; at >= 0; at--) if (t[at] == ch && --cnt == 0) return cmd == 'T' ? at + 1 : at;
    }
    return -1;
}
static mv_t motion(unsigned k, int cnt, int had, int op) {
    mv_t m = { 1, E.cy, E.cx, 0, 0 };
    switch (k) {
    case '\'': case '`': {                          /* a mark (v0.60.231) */
        int i = find_ch >= 'a' && find_ch <= 'z' ? find_ch - 'a' : -1;
        if (i < 0 || !marks[i].set) { msg("E20: Mark not set", 0, 0); m.ok = 0; break; }
        m.y = imin(marks[i].y, E.t.n - 1);
        if (k == '\'') { m.line = 1; m.x = first_nonblank(&E.t, m.y); }
        else m.x = imin(marks[i].x, imax(lenof(m.y) - (op ? 0 : 1), 0));
        break;
    }
    case 'f': case 't': case 'F': case 'T': case ';': case ',': {
        int cmd = (int)k, ch = find_ch, again = 0;
        if (k == ';' || k == ',') {
            if (!last_find) { m.ok = 0; break; }
            cmd = last_find; ch = last_find_ch; again = 1;
            if (k == ',') cmd = cmd == 'f' ? 'F' : cmd == 'F' ? 'f' : cmd == 't' ? 'T' : 't';
        } else { last_find = cmd; last_find_ch = ch; }
        int x = find_on_line(cmd, ch, cnt, again);
        if (x < 0) { m.ok = 0; break; }
        m.x = x; m.incl = cmd == 'f' || cmd == 't';
        break;
    }
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
/* gu gU g~ (v0.60.232): the case of every letter from (ay,ax) up to (by,bx),
 * bx not included; the cursor to the start, the register left alone. */
static int case_op(int op) { return op == 'u' || op == 'U' || op == '~'; }
static void change_case(int op, int ay, int ax, int by, int bx) {
    change_begin();
    for (int y = ay; y <= by; y++) {
        int x0 = y == ay ? ax : 0, x1 = y == by ? imin(bx, lenof(y)) : lenof(y);
        for (int x = x0; x < x1; x++) {
            char c = E.t.s[y][x];
            int lo = c >= 'a' && c <= 'z', up = c >= 'A' && c <= 'Z';
            if ((op == 'U' || op == '~') && lo) c = (char)(c - 32);
            else if ((op == 'u' || op == '~') && up) c = (char)(c + 32);
            E.t.s[y][x] = c;
        }
    }
    E.cy = ay; E.cx = ax; clamp(); E.want = E.cx;
}
static void op_lines(int op, int a, int b) {
    if (op == '>' || op == '<') { shift_lines(a, b, op == '>' ? 1 : -1); return; }
    a = imax(a, 0); b = imin(b, E.t.n - 1);
    if (case_op(op)) { int x = E.cx; change_case(op, a, 0, b, T_COLS + 1); E.cx = x; clamp(); E.want = E.cx; return; }   /* the column kept */
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
/* ---- text objects (v0.60.227), as neovim's: iw aw iW aW, i" a" i' a',
 * i( a( ib ab, i[ a[, i{ a{ iB aB, i< a<. The range is (ay,ax)..(by,bx),
 * the end inclusive; *line: whole lines ay..by. 0: none here. ---- */
static int wclass(char c, int big) { return c == ' ' || c == '\t' ? 0 : big || kw(c) ? 1 : 2; }
static int obj_word(int a, int big, int cnt, int *ax, int *bx) {
    const char *t = E.t.s[E.cy]; int n = len(), x = E.cx;
    if (!n) return 0;
    int c0 = wclass(t[x], big), s0 = x, e = x;
    while (s0 > 0 && wclass(t[s0 - 1], big) == c0) s0--;
    while (e + 1 < n && wclass(t[e + 1], big) == c0) e++;
    if (!a) {                                        /* iw: runs, white ones counted too */
        for (int k = 1; k < cnt && e + 1 < n; k++) { int c = wclass(t[e + 1], big); e++; while (e + 1 < n && wclass(t[e + 1], big) == c) e++; }
        *ax = s0; *bx = e; return 1;
    }
    if (c0 == 0) {                                   /* aw on white: it and the word after */
        for (int k = 0; k < cnt && e + 1 < n; k++) { int c = wclass(t[e + 1], big); e++; while (e + 1 < n && wclass(t[e + 1], big) == c) e++; }
        *ax = s0; *bx = e; return 1;
    }
    int trail = 0;
    for (int k = 0; k < cnt; k++) {
        if (k) {                                     /* the next word */
            if (e + 1 >= n) break;
            int c = wclass(t[e + 1], big); e++; while (e + 1 < n && wclass(t[e + 1], big) == c) e++;
        }
        int w = e + 1; while (w < n && wclass(t[w], big) == 0) w++;
        trail = w - 1 > e; e = w - 1;
    }
    if (!trail) while (s0 > 0 && wclass(t[s0 - 1], big) == 0) s0--;   /* none after: the white before */
    *ax = s0; *bx = e; return 1;
}
static int obj_quote(int a, char q, int *ax, int *bx) {
    const char *t = E.t.s[E.cy]; int n = len(), x = E.cx, before = 0, o = -1, c = -1;
    for (int i = 0; i < x && i < n; i++) if (t[i] == q && (i == 0 || t[i - 1] != '\\')) before++;
    int on = x < n && t[x] == q;
    if (on) {                                        /* on a quote: which one it is, by those before it */
        if (before % 2 == 0) { o = x; for (int i = x + 1; i < n; i++) if (t[i] == q && t[i - 1] != '\\') { c = i; break; } }
        else { c = x; for (int i = x - 1; i >= 0; i--) if (t[i] == q && (i == 0 || t[i - 1] != '\\')) { o = i; break; } }
    } else if (before % 2) {                         /* inside */
        for (int i = x - 1; i >= 0; i--) if (t[i] == q && (i == 0 || t[i - 1] != '\\')) { o = i; break; }
        for (int i = x + 1; i < n; i++) if (t[i] == q && t[i - 1] != '\\') { c = i; break; }
    } else {                                         /* before: the next pair on the line */
        for (int i = x + 1; i < n && c < 0; i++) if (t[i] == q && (i == 0 || t[i - 1] != '\\')) { if (o < 0) o = i; else c = i; }
    }
    if (o < 0 || c < 0) return 0;
    if (!a) { if (c - o < 2) return 0; *ax = o + 1; *bx = c - 1; return 1; }
    int s0 = o, e = c;
    if (e + 1 < n && (t[e + 1] == ' ' || t[e + 1] == '\t')) { while (e + 1 < n && (t[e + 1] == ' ' || t[e + 1] == '\t')) e++; }
    else while (s0 > 0 && (t[s0 - 1] == ' ' || t[s0 - 1] == '\t')) s0--;
    *ax = s0; *bx = e; return 1;
}
static int at_ch(int y, int x) { return x >= 0 && x < lenof(y) ? E.t.s[y][x] : '\n'; }
static int match_fwd(char o, char c, int y, int x, int *oy, int *ox) {   /* from the o at y,x to its c */
    int d = 0;
    for (;;) {
        int ch = at_ch(y, x);
        if (ch == o) d++; else if (ch == c && --d == 0) { *oy = y; *ox = x; return 1; }
        if (++x >= lenof(y)) { if (++y >= E.t.n) return 0; x = -1; }
    }
}
static int obj_block(int a, char o, char c, int cnt, int *ay, int *ax, int *by, int *bx, int *line) {
    int y = E.cy, x = E.cx, oy = -1, ox = -1, need = cnt, bal = 0;
    if (at_ch(y, x) == o) { oy = y; ox = x; need--; }
    while (need > 0) {                               /* back to the open that is not closed before the cursor */
        if (--x < 0) { if (--y < 0) break; x = lenof(y); continue; }
        int ch = at_ch(y, x);
        if (ch == c) bal++;
        else if (ch == o) { if (bal) bal--; else { oy = y; ox = x; need--; } }
    }
    if (need > 0) {                                  /* not inside one: the next on the line (neovim's) */
        if (cnt > 1) return 0;
        oy = -1;
        for (int i = E.cx; i < len(); i++) if (E.t.s[E.cy][i] == o) { oy = E.cy; ox = i; break; }
        if (oy < 0) return 0;
    }
    int cy, cx;
    if (!match_fwd(o, c, oy, ox, &cy, &cx)) return 0;
    *line = 0;
    if (a) { *ay = oy; *ax = ox; *by = cy; *bx = cx; return 1; }
    int sy = oy, sx = ox + 1, ey = cy, ex = cx - 1, smoved = 0, emoved = 0;
    if (sx >= lenof(oy) && oy < cy) { sy = oy + 1; sx = 0; smoved = 1; }
    int blank = 1; for (int i = 0; i < cx; i++) if (E.t.s[cy][i] != ' ' && E.t.s[cy][i] != '\t') blank = 0;
    if (blank && cy > oy) { ey = cy - 1; ex = lenof(ey) - 1; emoved = 1; }
    if (smoved && emoved) { if (sy > ey) return 0; *ay = sy; *by = ey; *line = 1; return 1; }
    if (sy > ey || (sy == ey && sx > ex)) return 0;
    *ay = sy; *ax = sx; *by = ey; *bx = ex; return 1;
}
static int textobj(int a, int k, int cnt, int *ay, int *ax, int *by, int *bx, int *line) {
    *line = 0; *ay = *by = E.cy;
    switch (k) {
    case 'w': return obj_word(a, 0, cnt, ax, bx);
    case 'W': return obj_word(a, 1, cnt, ax, bx);
    case '"': case '\'': case '`': return obj_quote(a, (char)k, ax, bx);
    case '(': case ')': case 'b': return obj_block(a, '(', ')', cnt, ay, ax, by, bx, line);
    case '[': case ']': return obj_block(a, '[', ']', cnt, ay, ax, by, bx, line);
    case '{': case '}': case 'B': return obj_block(a, '{', '}', cnt, ay, ax, by, bx, line);
    case '<': case '>': return obj_block(a, '<', '>', cnt, ay, ax, by, bx, line);
    }
    return 0;
}
static void op_apply(int op, mv_t m) {
    if (op == '>' || op == '<') { shift_lines(imin(E.cy, m.y), imax(E.cy, m.y), op == '>' ? 1 : -1); return; }
    if (m.line) { op_lines(op, imin(E.cy, m.y), imax(E.cy, m.y)); return; }
    int ay = E.cy, ax = E.cx, by = m.y, bx = m.x;
    if (by < ay || (by == ay && bx < ax)) { int t = ay; ay = by; by = t; t = ax; ax = bx; bx = t; }
    if (m.incl) bx++;
    if (by == ay) bx = imin(bx, lenof(ay));
    if (case_op(op)) { change_case(op, ay, ax, by, bx); return; }
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
static struct { int count, op, opcount, ophad, g, gcount, ghad, wait, wcount, whad, z; } P;   /* wait: f t F T r for a character */
static void replace_chars(int ch, int cnt) {        /* r (v0.60.222): none if the line is too short */
    if (E.cx + cnt > len()) return;
    change_begin();
    for (int i = 0; i < cnt; i++) E.t.s[E.cy][E.cx + i] = (char)ch;
    E.cx += cnt - 1; E.want = E.cx;
}
static void toggle_case(int cnt) {                   /* ~ (v0.60.222), on to the line's end at most */
    if (!len()) return;
    change_begin();
    int end = imin(E.cx + cnt, len());
    for (int i = E.cx; i < end; i++) {
        char c = E.t.s[E.cy][i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32); else if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        E.t.s[E.cy][i] = c;
    }
    E.cx = imin(end, len() - 1); E.want = E.cx;
}
/* Paging (v0.60.223), as neovim's with scrolloff 0: Ctrl-D Ctrl-U scroll
 * half (14, or the count, which is kept), the cursor going with the text;
 * Ctrl-F Ctrl-B a screen less two, the cursor to the new top or bottom row;
 * zz zt zb put the cursor's line in the middle, top, bottom. */
static int half = TEXT_ROWS / 2;
static void page(int k, int cnt, int had) {
    int last = E.t.n - 1;
    if (k == 4 || k == 0x15) {
        if (had) half = cnt;
        if (k == 4) {
            int maxtop = imax(last - TEXT_ROWS + 1, 0);
            if (E.cy == last) return;
            if (E.top < maxtop) E.top = imin(E.top + half, maxtop);
            E.cy = imin(E.cy + half, last);
        } else {
            if (E.cy == 0) return;
            E.top = imax(E.top - half, 0);
            E.cy = imax(E.cy - half, 0);
        }
    } else if (k == 6) {
        if (E.top >= last) return;
        E.top = E.top + TEXT_ROWS - 1 >= last ? last : imin(E.top + TEXT_ROWS - 2, last);   /* the end on screen: it goes to the top */
        E.cy = E.top;
    } else if (k == 2) {
        if (E.top == 0) return;
        E.top = imax(E.top - (TEXT_ROWS - 2), 0);
        E.cy = imin(E.top + TEXT_ROWS - 1, last);
    }
    E.cx = E.want; clamp();
}
static void normal_key(unsigned k) {
    if (P.wait) {                                   /* the character after f t F T r */
        int w = P.wait, cnt = P.wcount, had = P.whad;
        P.wait = 0;
        if (k == 27 || k >= 256) { P.op = 0; return; }
        if (w == 'r') { if (!P.op) replace_chars((int)k, cnt); P.op = 0; return; }
        if (w == 'm') { if (k >= 'a' && k <= 'z') { marks[k - 'a'].y = E.cy; marks[k - 'a'].x = E.cx; marks[k - 'a'].set = 1; } return; }
        if (w == 'i' || w == 'a') {                  /* an operator on a text object (v0.60.227) */
            int op = P.op, ay, ax, by, bx, line;
            int total = cnt * P.opcount;
            P.op = 0;
            if (!textobj(w == 'a', (int)k, total, &ay, &ax, &by, &bx, &line)) return;
            if (line) { op_lines(op, ay, by); return; }
            E.cy = ay; E.cx = ax;
            mv_t m = { 1, by, bx, 0, 1 };
            op_apply(op, m);
            return;
        }
        find_ch = (int)k;
        if (P.op) {
            int op = P.op, total = cnt * P.opcount;
            P.op = 0;
            mv_t m = motion((unsigned)w, total, had || P.ophad, 1);
            if (m.ok) op_apply(op, m);
            return;
        }
        mv_t m = motion((unsigned)w, cnt, had, 0);
        if (m.ok) { E.cy = m.y; E.cx = m.x; clamp(); E.want = E.cx; }
        return;
    }
    if (k == 27) { P.count = P.op = P.g = 0; return; }
    if ((k >= '1' && k <= '9') || (k == '0' && P.count)) { P.count = P.count * 10 + (int)(k - '0'); if (P.count > 99999) P.count = 99999; return; }
    int had = P.count > 0, cnt = had ? P.count : 1;
    P.count = 0;
    if (P.z) {                                      /* (v0.60.223) z then: zz zt zb */
        P.z = 0;
        if (k == 'z') E.top = imax(E.cy - (TEXT_ROWS / 2 - 1), 0);
        else if (k == 't') E.top = E.cy;
        else if (k == 'b') E.top = imax(E.cy - TEXT_ROWS + 1, 0);
        return;
    }
    if (!P.op && (k == 4 || k == 0x15 || k == 6 || k == 2)) { page((int)k, cnt, had); return; }
    if (!P.op && !P.g && k == 'z') { P.z = 1; return; }
    if (P.g) {                                      /* g then: gg, or gu gU g~ (v0.60.232) */
        P.g = 0;
        if (k == 'u' || k == 'U' || k == '~') {
            if (P.op == (int)k) { int op = P.op, total = P.gcount * P.opcount; P.op = 0; op_lines(op, E.cy, E.cy + total - 1); return; }   /* gugu gUgU g~g~ */
            if (P.op) { P.op = 0; return; }
            P.op = (int)k; P.opcount = P.gcount; P.ophad = P.ghad;
            return;
        }
        if (k != 'g') { P.op = 0; return; }
        k = 'g' + 256; cnt = P.gcount; had = P.ghad;
    } else if (k == 'g') { P.g = 1; P.gcount = cnt; P.ghad = had; return; }
    if (k == 'f' || k == 't' || k == 'F' || k == 'T' || (k == 'r' && !P.op) || (P.op && (k == 'i' || k == 'a'))
            || (k == 'm' && !P.op) || k == '\'' || k == '`') { P.wait = (int)k; P.wcount = cnt; P.whad = had; return; }
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
    case '*': star(1, cnt); break;
    case '#': star(-1, cnt); break;
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
    case '~': toggle_case(cnt); break;
    case 'u': { int n = 0; for (int i = 0; i < cnt; i++) n += undo_undo(&E.t, &E.cy, &E.cx, &E.seq); after_undo(); if (!n) msg("Already at oldest change", 0, 0); break; }
    case 0x12: { int n = 0; for (int i = 0; i < cnt; i++) n += undo_redo(&E.t, &E.cy, &E.cx, &E.seq); after_undo(); if (!n) msg("Already at newest change", 0, 0); break; }
    case 'i': insert_mode(E.cx, 1); ins_count = cnt; break;
    case 'a': insert_mode(len() ? E.cx + 1 : 0, 1); ins_count = cnt; break;
    case 'A': insert_mode(len(), 1); ins_count = cnt; break;
    case 'I': insert_mode(len() ? first_nonblank(&E.t, E.cy) : 0, 1); ins_count = cnt; break;
    case 'o': { int ind = indent_of(E.cy); change_begin(); if (text_insert_line(&E.t, E.cy + 1) == 0) { E.cy++; put_indent(E.cy, ind); insert_mode(ind, 0); ai_pending = ind; ins_count = cnt; ins_open = 1; } break; }
    case 'O': { int ind = indent_of(E.cy); change_begin(); if (text_insert_line(&E.t, E.cy) == 0) { put_indent(E.cy, ind); insert_mode(ind, 0); ai_pending = ind; ins_count = cnt; ins_open = 1; } break; }
    case ':': E.mode = M_COMMAND; E.cmdch = ':'; E.cmdn = 0; break;
    }
}
/* ---- visual mode (v0.60.206): the motions move the cursor, an operator
 * takes from where v or V was pressed to it ---- */
static char rec[256], cur[256];                     /* . : the last change's keys, and this command's */
static int recn, curn, cur_change, replaying;
static void visual_key(unsigned k) {
    if (P.wait == 'i' || P.wait == 'a') {           /* (v0.60.227) a text object: the selection becomes it */
        int a = P.wait == 'a', cnt = P.wcount, ay, ax, by, bx, line;
        P.wait = 0;
        if (k == 27 || !textobj(a, (int)k, cnt, &ay, &ax, &by, &bx, &line)) return;
        E.vy = ay; E.vx = line ? 0 : ax; E.cy = by; E.cx = line ? imax(lenof(by) - 1, 0) : bx;
        if (line) E.mode = M_VLINE;
        E.want = E.cx;
        return;
    }
    if (k == 27) { E.mode = M_NORMAL; P.count = 0; return; }
    if ((k >= '1' && k <= '9') || (k == '0' && P.count)) { P.count = P.count * 10 + (int)(k - '0'); return; }
    int had = P.count > 0, cnt = had ? P.count : 1;
    P.count = 0;
    if (P.g) {
        P.g = 0;
        if (k == 'g') { mv_t m = motion('g' + 256, cnt, had, 0); E.cy = m.y; E.cx = E.want; clamp(); return; }
        if (k != 'u' && k != 'U' && k != '~') return;  /* gu gU g~ there: as u U ~ (v0.60.232) */
    }
    if (k == 'g') { P.g = 1; return; }
    if (k == 'i' || k == 'a') { P.wait = (int)k; P.wcount = cnt; return; }
    if (k == 'v' || k == 'V') { int want = k == 'v' ? M_VISUAL : M_VLINE; E.mode = E.mode == want ? M_NORMAL : want; return; }
    int op = k == 'x' ? 'd' : (int)k;
    if (op == 'd' || op == 'y' || op == 'c' || op == '>' || op == '<' || case_op(op)) {
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
static void help_key(unsigned k) {                  /* (v0.60.219) the help page: scroll, :, q; no changes */
    static int g;
    int last = help_lines > TEXT_ROWS ? help_lines - TEXT_ROWS : 0;
    if (g) { g = 0; if (k == 'g') { E.htop = 0; return; } }
    if (k == 'j' || k == KEY_DOWN) { if (E.htop < last) E.htop++; }
    else if (k == 'k' || k == KEY_UP) { if (E.htop > 0) E.htop--; }
    else if (k == 'G') E.htop = last;
    else if (k == 'g') g = 1;
    else if (k == ':') { E.mode = M_COMMAND; E.cmdch = ':'; E.cmdn = 0; }
    else if (k == 'q') E.help = 0;
    else if (k != 27) msg(E21, 0, 0);
}
static int idle(void) { return E.mode == M_NORMAL && !P.count && !P.op && !P.g && !P.wait && !P.z; }
static int key(unsigned k) {                        /* 1: end neosim */
    if (!replaying) {                               /* the keys of a change, for . */
        if (idle() && k == '.') { E.msg[0] = 0; dot(); return 0; }
        if (k == '.' && E.mode == M_NORMAL && P.count && !P.op && !P.g && !P.wait && !P.z) {   /* (v0.60.234) 3.: the count in place of the change's own */
            int c = P.count; P.count = 0; E.msg[0] = 0; curn = 0;
            if (!recn) return 0;
            char d[8]; int dn = 0; do { d[dn++] = (char)('0' + c % 10); c /= 10; } while (c);
            int skip = 0; while (skip < recn && rec[skip] >= '0' && rec[skip] <= '9' && !(skip == 0 && rec[0] == '0')) skip++;
            char again[256]; int n = 0;
            while (dn && n < 255) again[n++] = d[--dn];
            for (int i = skip; i < recn && n < 255; i++) again[n++] = rec[i];
            for (int i = 0; i < n; i++) rec[i] = again[i];
            recn = n;
            dot();
            return 0;
        }
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
                if (E.cmdn) last_whole = 0;                 /* a typed pattern is plain text */
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
    if (E.help) { help_key(k); return 0; }
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
    text_lines_hook = marks_lines;
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
            else if (ev.type == WEV_CLOSE) { E.help = 0; done = command((char[]){ 'q', 0 }); }
            g = sys_win_event(win, &ev, 0);
        }
    }
    eprint("[neosim] bye\n");
    sys_win_close(win);
    return 0;
}

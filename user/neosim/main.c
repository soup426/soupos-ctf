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
 * gu gU g~ and in visual u U ~ (v0.60.232); counts on inserts, 3. (v0.60.234);
 * paragraphs { } ip ap (v0.60.235); :set nu rnu (v0.60.236); registers "a-"z "0 "1-"9 (v0.60.240);
 * macros q @ @@ (v0.60.241); ranges, :d :g :v :normal :sort (v0.60.247); Ctrl-A Ctrl-X (v0.60.248)
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
static char gmark[T_MAXL + 1];                       /* (v0.60.247) :g's lines still to do; moved as lines move */
static int g_active;
static void op_lines(int op, int a, int b);
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
    int w = SCR_COLS - num_w(&E);
    if (E.cx < E.left) E.left = E.cx;
    if (E.cx >= E.left + w) E.left = E.cx - w + 1;
}

/* ---- changes and undo ---- */
static int cur_change_hook;                         /* set by change_begin: . keeps the command */
static int grouping, group_pushed, pushed_last;     /* (v0.60.241) a macro played is one undo step */
static void change_begin(void) {                    /* before every change: a step back to this */
    cur_change_hook = 1;
    pushed_last = !grouping || !group_pushed;
    if (pushed_last) { undo_push(&E.t, E.cy, E.cx, E.seq); group_pushed = 1; }
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
/* Registers (v0.60.240): "0-"9 and "a-"z beside the unnamed one above.
 * "x before a command names one (reg_sel); "a-"z take a copy, "A-"Z add to
 * theirs; with none named a yank also goes to "0, and a delete or change
 * of whole lines to "1, the older ones moving down to "9. */
#define BANK_MAX 8192
static char bank[36][BANK_MAX];                      /* 0-9: "0-"9; 10-35: "a-"z */
static int bank_n[36], bank_lines[36];
static int reg_sel, reg_fresh;                       /* the register named for this command; just named */
static int rec_reg, rec_n, play_depth, last_play;    /* (v0.60.241) macros: being recorded into, played */
static char rec_buf[BANK_MAX];
static void macro_play(int r, int cnt);
static int bank_of(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? 10 + c - 'a' : c >= 'A' && c <= 'Z' ? 10 + c - 'A' : -1; }
static void bank_put(int b, const char *s, int n, int lines) {
    if (n > BANK_MAX) n = BANK_MAX;
    for (int i = 0; i < n; i++) bank[b][i] = s[i];
    bank_n[b] = n; bank_lines[b] = lines;
}
static void reg_after(int op) {                      /* after a yank, delete or change took the text into reg */
    int b = bank_of(reg_sel);
    if (reg_sel >= 'A' && reg_sel <= 'Z' && (bank_n[b] || bank_lines[b])) {   /* added to */
        int lines = bank_lines[b] || reg_lines, n = bank_n[b];
        if (lines && n < BANK_MAX) bank[b][n++] = '\n';
        for (int i = 0; i < reg_n && n < BANK_MAX; i++) bank[b][n++] = reg[i];
        bank_n[b] = n; bank_lines[b] = lines;
        reg_n = 0; for (int i = 0; i < n; i++) reg_add(bank[b][i]);   /* "" is what "A holds now, as vim's */
        reg_lines = lines;
    } else if (b >= 10) bank_put(b, reg, reg_n, reg_lines);
    else if (!reg_sel) {
        if (op == 'y') bank_put(0, reg, reg_n, reg_lines);
        else if (reg_lines) {
            for (int i = 9; i > 1; i--) bank_put(i, bank[i - 1], bank_n[i - 1], bank_lines[i - 1]);
            bank_put(1, reg, reg_n, reg_lines);
        }
    }
    reg_sel = 0;
}
/* A join that would pass 255 columns (v0.60.286, review R-N5): refused,
 * said, logged, before anything is changed. 1 if refused. */
static int too_long_join(int total) {
    if (total <= T_COLS) return 0;
    msg("E1240: Resulting text too long", 0, 0);
    eprint("[neosim] refused a join past 255\n");
    return 1;
}
static void delete_chars(int ay, int ax, int by, int bx) {
    if (ay != by && too_long_join(ax + lenof(by) - bx)) return;
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

/* ---- search and substitute: vim's patterns (v0.60.243; plain text v0.60.206) ----
 * A pattern is vim's with 'magic' on, turned into the POSIX ERE that
 * user/rx.c runs: \+ \? \= \| \( \) \{ \} are its operators and a bare + ? |
 * ( ) { } is a character; ^ and $ are anchors only at the ends (or by \( \|
 * \)); \< \> \w \s as they are, \d [0-9]; [...] as written. */
static unsigned char rx_mem[192 * 1024];             /* rx.c's lists, made again for each line */
static unsigned rx_used;
static void *rx_take(unsigned n) {
    n = (n + 7) & ~7u;
    if (rx_used + n > sizeof rx_mem) return 0;
    void *p = rx_mem + rx_used; rx_used += n; return p;
}
#define RX_ALLOC(n) rx_take(n)
#define RX_FREE(p)  ((void)(p))
#include "../rx.c"
static char last_pat[80];
static int last_dir = 1;
static int kw(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (unsigned char)c >= 192; }
static int vim2ere(const char *v, char *o, int cap) {
    int n = 0, k = 0, start = 1;
    while (v[n]) n++;
#define PUT(c) do { if (k >= cap - 1) return -1; o[k++] = (char)(c); } while (0)
    for (int i = 0; i < n; i++) {
        char c = v[i];
        int was_start = start; start = 0;
        if (c == '\\' && i + 1 < n) {
            char d = v[++i];
            if (d == '+' || d == '?' || d == '|' || d == '(' || d == ')' || d == '{' || d == '}') { PUT(d); start = d == '|' || d == '('; }
            else if (d == '=') PUT('?');
            else if (d == 'd' || d == 'D') { const char *q = d == 'd' ? "[0-9]" : "[^0-9]"; while (*q) PUT(*q++); }
            else { PUT('\\'); PUT(d); }               /* \< \> \w \s, and the rest literal */
        } else if (c == '[') {                       /* a class as written, if it closes */
            int j = i + 1;
            if (j < n && v[j] == '^') j++;
            if (j < n && v[j] == ']') j++;
            while (j < n && v[j] != ']') j++;
            if (j >= n) { PUT('\\'); PUT('['); }
            else { for (; i <= j; i++) PUT(v[i]); i = j; }
        } else if (c == '+' || c == '?' || c == '|' || c == '(' || c == ')' || c == '{' || c == '}' || c == '~') { PUT('\\'); PUT(c); }
        else if (c == '^' && !was_start) { PUT('\\'); PUT('^'); }
        else if (c == '$' && !(i == n - 1 || (v[i + 1] == '\\' && i + 2 < n && (v[i + 2] == '|' || v[i + 2] == ')')))) { PUT('\\'); PUT('$'); }
        else if (c == '*' && was_start) { PUT('\\'); PUT('*'); }
        else PUT(c);
    }
#undef PUT
    o[k] = 0;
    return 0;
}
static rx_t RX;
static int rx_sub[2 * (RX_MAXGRP + 1)];
static int pat_compile(const char *p) {             /* 0, or -1 a pattern rx cannot take */
    char e[512];
    if (vim2ere(p, e, sizeof e) < 0) return -1;
    return rx_compile(&RX, e, 0) < 0 ? -1 : 0;
}
static int rx_at(int y, int from, int *s, int *e) { /* the first match on line y starting at or after from */
    if (from > lenof(y)) return 0;
    rx_used = 0;
    if (rx_exec(&RX, E.t.s[y], lenof(y), from, rx_sub) <= 0) return 0;
    *s = rx_sub[0]; *e = rx_sub[1];
    return 1;
}
/* The next match from just after (or before) the cursor, round the end of
 * the text once; 1 found (at *oy,*ox), 0 none. *wrapped: it went round. */
static int find(int dir, const char *p, int *oy, int *ox, int *wrapped) {
    *wrapped = 0;
    if (!p[0] || pat_compile(p) < 0) return 0;
    int y = E.cy, x = E.cx, s, e;
    for (int step = 0; step <= E.t.n; step++) {
        if (dir > 0) {
            if (rx_at(y, step == 0 ? x + 1 : 0, &s, &e)) {
                /* (v0.60.297) a match at the line's end lands on its last
                 * character: one there already is not after the cursor, as
                 * nvim's /$ then n goes on to the next line's end. */
                int land = s < lenof(y) ? s : (lenof(y) ? lenof(y) - 1 : 0);
                if (!(step == 0 && land <= x)) { *oy = y; *ox = s; return 1; }
            }
            if (++y >= E.t.n) { y = 0; *wrapped = 1; }
        } else {
            int limit = step == 0 ? x : lenof(y) + 1, best = -1, f = 0;   /* the last that starts before limit */
            while (rx_at(y, f, &s, &e) && s < limit) { best = s; f = s + 1; }
            if (best >= 0) { *oy = y; *ox = best; return 1; }
            if (--y < 0) { y = E.t.n - 1; *wrapped = 1; }
        }
    }
    return 0;                                        /* the last step came back to the cursor's line, all of it */
}
static void search(int dir, const char *p) {
    int y, x, w;
    if (!find(dir, p, &y, &x, &w)) { msg("E486: Pattern not found: ", p, 0); return; }
    E.cy = y; E.cx = x;
    clamp(); E.want = E.cx;                          /* (v0.60.297, review R-N32) /$ matches past the last character */
    if (w) msg(dir > 0 ? "search hit BOTTOM, continuing at TOP" : "search hit TOP, continuing at BOTTOM", 0, 0);
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
    last_pat[k++] = '\\'; last_pat[k++] = '<';         /* (v0.60.243) as neovim's: \<word\> */
    while (e < n && kw(t[e]) && k < (int)sizeof(last_pat) - 3) last_pat[k++] = t[e++];
    last_pat[k++] = '\\'; last_pat[k++] = '>'; last_pat[k] = 0;
    last_dir = dir;
    E.cx = x;
    for (int i = 0; i < cnt; i++) search(dir, last_pat);
}
/* :s/PAT/REP/[g] on line a..b; the whole job one undo step. */
static void substitute(int a, int b, char *spec) {
    char *pat = spec, *rep, *fl;
    rep = pat; while (*rep && *rep != '/') { if (*rep == '\\' && rep[1]) rep++; rep++; }   /* \/ is not the end */
    if (!*rep) { msg("E486: Pattern not found: ", pat, 0); return; }
    *rep++ = 0;
    fl = rep; while (*fl && *fl != '/') { if (*fl == '\\' && fl[1]) fl++; fl++; }
    if (*fl) *fl++ = 0;
    int all = 0; for (char *f = fl; *f; f++) if (*f == 'g') all = 1;
    if (!pat[0]) { if (!last_pat[0]) { msg("E35: No previous regular expression", 0, 0); return; } pat = last_pat; }
    else scopy(last_pat, pat, sizeof(last_pat));
    if (pat_compile(pat) < 0) { msg("E486: Pattern not found: ", pat, 0); return; }
    int subs = 0, lines = 0, last = -1, begun = 0;
    for (int y = a; y <= b; y++) {
        int did = 0, x = 0, ms, me;
        while (rx_at(y, x, &ms, &me)) {
            char out[T_COLS + 1]; int rl = 0;               /* the replacement: & the match, \1-\9 its groups */
            for (const char *q = rep; *q && rl < T_COLS; q++) {
                int g = -1;
                if (*q == '&') g = 0;
                else if (*q == '\\' && q[1] >= '0' && q[1] <= '9') g = *++q - '0';
                else if (*q == '\\' && q[1]) { out[rl++] = *++q; continue; }
                if (g < 0) { out[rl++] = *q; continue; }
                int gs = rx_sub[2 * g], ge = rx_sub[2 * g + 1];
                for (int i = gs; gs >= 0 && i < ge && rl < T_COLS; i++) out[rl++] = E.t.s[y][i];
            }
            if (lenof(y) - (me - ms) + rl > T_COLS) break;  /* it would not fit */
            if (!begun) { change_begin(); begun = 1; }
            for (int i = ms; i < me; i++) text_delete_char(&E.t, y, ms);
            for (int i = 0; i < rl; i++) text_insert_char(&E.t, y, ms + i, out[i]);
            subs++; did = 1;
            x = ms + rl + (me == ms);                        /* past an empty match, on */
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
    /* As vim's (v0.60.278, review R-N3): a buffer with no name takes the
     * one written; a named buffer keeps its own, and only a write to it
     * clears [+]. Taking OTHER's name, :w OTHER then :q quit with the edit
     * never written to the file it came from. */
    int own = path == E.path || !E.path[0] || same(path, E.path);
    if (!E.path[0]) scopy(E.path, path, sizeof(E.path));
    if (own) { E.saved = E.seq; E.changed = 0; }
    msg_num("\"", path, bytes, "B written");
    return 0;
}
static const char E21[] = "E21: Cannot make changes, 'modifiable' is off";
/* ---- :g :v :normal :sort and ranges (v0.60.247) ---- */
static int range_line(char **pc, int *out) {         /* N . or $, as a line index; 0 if none */
    char *c = *pc;
    if (*c == '.') { *out = E.cy; c++; }
    else if (*c == '$') { *out = E.t.n - 1; c++; }
    else if (*c >= '0' && *c <= '9') { int n = 0; while (*c >= '0' && *c <= '9') n = n * 10 + (*c++ - '0'); *out = n - 1; }
    else return 0;
    *pc = c;
    return 1;
}
static int command(char *c);
static int key(unsigned k);
static void normal_cmd(int a, int b, const char *keys) {
    int outer = !grouping;
    if (outer) { grouping = 1; group_pushed = 0; }
    for (int y = a; y <= b && y < E.t.n; y++) {
        E.cy = y; E.cx = 0; E.want = 0; E.mode = M_NORMAL;
        for (const char *q = keys; *q; q++) key((unsigned)(unsigned char)*q);
        if (E.mode != M_NORMAL) key(27);             /* an insert not ended is ended */
    }
    if (outer) grouping = 0;
}
static void global_cmd(int a, int b, int inv, char *spec) {
    char *pat = spec, *cmd = spec;
    while (*cmd && *cmd != '/') { if (*cmd == '\\' && cmd[1]) cmd++; cmd++; }
    if (*cmd) *cmd++ = 0;
    if (!pat[0]) { if (!last_pat[0]) { msg("E35: No previous regular expression", 0, 0); return; } pat = last_pat; }
    else scopy(last_pat, pat, sizeof(last_pat));
    if (pat_compile(pat) < 0) { msg("E486: Pattern not found: ", pat, 0); return; }
    int any = 0, s0, e0;
    for (int y = 0; y < E.t.n; y++) {
        int m = y >= a && y <= b && (rx_at(y, 0, &s0, &e0) != inv);
        gmark[y] = (char)m; any |= m;
    }
    if (!any) { msg("E486: Pattern not found: ", pat, 0); return; }
    char saved[80];
    scopy(saved, cmd, sizeof(saved));
    int outer = !grouping;
    if (outer) { grouping = 1; group_pushed = 0; }
    g_active = 1;
    for (;;) {                                       /* the first line still marked, each time */
        int y = 0;
        while (y < E.t.n && !gmark[y]) y++;
        if (y >= E.t.n) break;
        gmark[y] = 0;
        E.cy = y; E.cx = 0; E.want = 0;
        char one[80];
        scopy(one, saved, sizeof(one));
        command(one[0] ? one : (char[]){ 'p', 0 });
    }
    g_active = 0;
    if (outer) grouping = 0;
}
static int first_num(int y, int *v) {                /* the first decimal number on the line, a - before it kept */
    const char *t = E.t.s[y]; int n = lenof(y);
    for (int i = 0; i < n; i++) if (t[i] >= '0' && t[i] <= '9') {
        int neg = i > 0 && t[i - 1] == '-', x = 0;
        while (i < n && t[i] >= '0' && t[i] <= '9') x = x * 10 + (t[i++] - '0');
        *v = neg ? -x : x; return 1;
    }
    return 0;
}
static int line_cmp(int y1, int y2, int num) {       /* <0 0 >0, as sort compares two lines */
    if (num) {
        int a, b, ha = first_num(y1, &a), hb = first_num(y2, &b);
        if (!ha || !hb) return ha - hb;              /* no number: before those with one */
        return a < b ? -1 : a > b;
    }
    int la = lenof(y1), lb = lenof(y2);
    for (int i = 0; i < la && i < lb; i++) {
        unsigned char p = (unsigned char)E.t.s[y1][i], q = (unsigned char)E.t.s[y2][i];
        if (p != q) return p < q ? -1 : 1;
    }
    return la - lb;
}
static void line_swap(int y1, int y2) { char *t = E.t.s[y1]; E.t.s[y1] = E.t.s[y2]; E.t.s[y2] = t; int l = E.t.len[y1]; E.t.len[y1] = E.t.len[y2]; E.t.len[y2] = l; }
static void sort_cmd(int a, int b, const char *c) {
    int rev = 0, num = 0, uniq = 0;
    while (*c && *c != ' ' && *c != '!') c++;
    if (*c == '!') { rev = 1; c++; }
    for (; *c; c++) { if (*c == 'n') num = 1; else if (*c == 'u') uniq = 1; }
    change_begin();
    for (int i = a + 1; i <= b; i++)                 /* insertion: stable, so equal lines keep their order */
        for (int j = i; j > a; j--) {
            int d = line_cmp(j - 1, j, num);
            if (rev ? d < 0 : d > 0) line_swap(j - 1, j); else break;
        }
    if (uniq)
        for (int y = a + 1; y <= b; ) {
            if (line_cmp(y - 1, y, 0) == 0) { text_delete_line(&E.t, y); b--; } else y++;
        }
    E.cy = a; E.cx = 0; clamp(); E.want = E.cx;
}
/* :reg (v0.60.240): the registers as neovim's :registers lists them, on the
 * read-only page :help uses: "Type Name Content", then "" "0-"9 "a-"z that
 * hold anything, l lines or c characters, a new line as ^J. */
static char reg_rows[40][SCR_COLS + 1];
static const char *reg_ptr[40];
static void reg_row(int *n, int name, const char *t, int len, int lines) {
    if (*n >= 40 || (!len && !lines)) return;
    char *o = reg_rows[*n]; int k = 0;
    const char *head = lines ? "  l  \"" : "  c  \"";
    while (*head) o[k++] = *head++;
    o[k++] = (char)name; o[k++] = ' '; o[k++] = ' '; o[k++] = ' ';
    for (int i = 0; i <= len && k < SCR_COLS; i++) {
        if (i == len) { if (lines && k + 1 < SCR_COLS) { o[k++] = '^'; o[k++] = 'J'; } break; }
        if (t[i] == '\n') { if (k + 1 < SCR_COLS) { o[k++] = '^'; o[k++] = 'J'; } }
        else o[k++] = t[i];
    }
    o[k] = 0; reg_ptr[*n] = o; (*n)++;
}
static void reg_page(void) {
    int n = 0;
    scopy(reg_rows[0], "Type Name Content", SCR_COLS + 1); reg_ptr[n++] = reg_rows[0];
    reg_row(&n, '"', reg, reg_n, reg_lines);
    for (int b = 0; b < 36; b++) reg_row(&n, b < 10 ? '0' + b : 'a' + b - 10, bank[b], bank_n[b], bank_lines[b]);
    E.help = 1; E.htop = 0; E.page = reg_ptr; E.pagen = n; E.pagename = "registers [RO]";
    eprint("[neosim] registers\n");
}
static int command(char *c) {                        /* 1: end neosim */
    while (*c == ' ') c++;
    if (E.help) {                                    /* (v0.60.219) on the help page: only leaving it, or :e */
        if (same(c, "q") || same(c, "q!") || same(c, "close")) { E.help = 0; return 0; }
        if (same(c, "help") || same(c, "h")) return 0;
        if (c[0] == 'w' || same(c, "x")) { msg("E382: Cannot write, 'buftype' option is set", 0, 0); return 0; }
        if (!(c[0] == 'e' && (c[1] == ' ' || c[1] == '!'))) { msg(E21, 0, 0); return 0; }
        E.help = 0;
    }
    /* A range (v0.60.247): % the whole text, N . $ a line, two of them with
     * a comma; then s/ d sort normal, and g/ v/ g!/ (whole text by default). */
    int ra = E.cy, rb = E.cy, ranged = 0;
    if (*c == '%') { ra = 0; rb = E.t.n - 1; c++; ranged = 1; }
    else if ((*c >= '0' && *c <= '9') || *c == '.' || *c == '$') {
        ranged = 1;
        if (!range_line(&c, &ra)) { msg("E16: Invalid range", 0, 0); return 0; }
        rb = ra;
        if (*c == ',') { c++; if (!range_line(&c, &rb)) { msg("E16: Invalid range", 0, 0); return 0; } }
        if (ra > rb) { int t = ra; ra = rb; rb = t; }
        if (ra < 0 || rb >= E.t.n) { msg("E16: Invalid range", 0, 0); return 0; }
    }
    if (c[0] == 's' && c[1] == '/') { substitute(ra, rb, c + 2); return 0; }   /* :s keeps its spaces */
    if ((c[0] == 'g' || c[0] == 'v') && (c[1] == '/' || (c[0] == 'g' && c[1] == '!' && c[2] == '/'))) {
        int inv = c[0] == 'v' || c[1] == '!';
        if (!ranged) { ra = 0; rb = E.t.n - 1; }
        global_cmd(ra, rb, inv, c + (c[1] == '!' ? 3 : 2));
        return 0;
    }
    if (c[0] == 'n' && c[1] == 'o' && c[2] == 'r' && c[3] == 'm') {   /* :norm :normal KEYS */
        char *k = c; while (*k && *k != ' ') k++;
        while (*k == ' ') k++;
        normal_cmd(ra, rb, k);
        return 0;
    }
    if (same(c, "d") || same(c, "delete")) { op_lines('d', ra, rb); return 0; }   /* :d, the cursor's line without a range */
    if (c[0] == 's' && c[1] == 'o' && c[2] == 'r') {                   /* :sort[!] [n] [u] */
        if (!ranged) { ra = 0; rb = E.t.n - 1; }
        sort_cmd(ra, rb, c);
        return 0;
    }
    if (ranged && !*c) { E.cy = rb; E.cx = first_nonblank(&E.t, rb); E.want = E.cx; return 0; }   /* :N goes there */
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
        if (write_file(*arg ? arg : E.path) != 0) return 0;
        /* Written somewhere else (v0.60.284, review R-N30): the buffer's own
         * file still lacks the edit, so not a quit, as :q refuses. */
        if (E.changed) { msg("E37: No write since last change (add ! to override)", 0, 0); eprint("[neosim] refused :wq (changed)\n"); return 0; }
        return 1;
    }
    if (same(c, "set") || same(c, "se")) {          /* (v0.60.236) :set nu nonu rnu nornu (and the long names), several at once */
        if (!*arg) { msg(E.nu ? "  number" : "nonumber", E.rnu ? "  relativenumber" : "  norelativenumber", 0); return 0; }
        while (*arg) {
            char *o = arg; while (*arg && *arg != ' ') arg++;
            if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; }
            if (same(o, "nu") || same(o, "number")) E.nu = 1;
            else if (same(o, "nonu") || same(o, "nonumber")) E.nu = 0;
            else if (same(o, "rnu") || same(o, "relativenumber")) E.rnu = 1;
            else if (same(o, "nornu") || same(o, "norelativenumber")) E.rnu = 0;
            else { msg("E518: Unknown option: ", o, 0); return 0; }
        }
        return 0;
    }
    if (same(c, "e") || same(c, "e!")) {
        if (!*arg) { msg("E32: No file name", 0, 0); return 0; }
        if (E.changed && same(c, "e")) { msg("E37: No write since last change (add ! to override)", 0, 0); return 0; }
        open_file(arg);
        return 0;
    }
    if (same(c, "help") || same(c, "h")) { E.help = 1; E.htop = 0; E.page = help_text; E.pagen = help_lines; E.pagename = "help.txt [Help][RO]"; eprint("[neosim] help\n"); return 0; }
    if (same(c, "reg") || same(c, "registers") || same(c, "di") || same(c, "display")) { reg_page(); return 0; }
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
static int ins_full;                                 /* a split refused in this insert (v0.60.304) */
/* The buffer is at its most lines: said, and logged for the tests (v0.60.304). */
static void full_msg(void) { msg("E: the file is full (2048 lines)", 0, 0); eprint("[neosim] the file is full\n"); }
static void insert_mode(int x, int push) {
    ins_count = 1; ins_open = 0; ins_n = 0; ins_full = 0;
    if (push) change_begin();
    E.ins_dirty = !push;                            /* an operator before it already changed the text */
    E.mode = M_INSERT; E.cx = x; clamp(); E.want = E.cx;
}
static void insert_key(unsigned k) {
    if (k == 27 && ins_count > 1 && ins_n && !ins_again) {
        ins_again = 1;
        for (int r = 1; r < ins_count && !ins_full; r++) {   /* not on past a refused split */
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
        if (!E.ins_dirty && pushed_last) { undo_discard(&E.seq); E.changed = E.seq != E.saved; if (grouping) group_pushed = 0; }   /* nothing typed: no step */
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
        } else { ins_full = 1; full_msg(); }        /* was silent: what came next ran onto this line */
        return;
    }
    if (k == '\b') {
        if (E.cx > 0) { text_delete_char(&E.t, E.cy, E.cx - 1); E.cx--; E.want = E.cx; E.ins_dirty = 1; }
        else if (E.cy > 0 && !too_long_join(lenof(E.cy - 1) + len())) { int at = lenof(E.cy - 1); text_join(&E.t, E.cy - 1); E.cy--; E.cx = E.want = at; E.ins_dirty = 1; }
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
    if (g_active) {                                  /* (v0.60.247) :g's lines still to do move too */
        if (delta > 0) { for (int i = E.t.n - 1; i > at; i--) gmark[i] = gmark[i - 1]; gmark[at] = 0; }
        else { for (int i = at; i < E.t.n; i++) gmark[i] = gmark[i + 1]; gmark[E.t.n] = 0; }
    }
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
/* Paragraphs (v0.60.235), as neovim's findpar: an empty line bounds one. */
static int blank(int y) { return lenof(y) == 0; }
static int findpar(int dir, int cnt, int *eof) {    /* the line } or { lands on */
    int curr = E.cy;
    *eof = 0;
    while (cnt--) {
        int skipped = 0;
        for (int first = 1; ; first = 0) {
            if (!blank(curr)) skipped = 1;
            if (!first && skipped && blank(curr)) break;
            curr += dir;
            if (curr < 0 || curr >= E.t.n) { curr -= dir; *eof = 1; break; }
        }
        if (*eof) break;
    }
    return curr;
}
static mv_t motion(unsigned k, int cnt, int had, int op) {
    mv_t m = { 1, E.cy, E.cx, 0, 0 };
    switch (k) {
    case '}': case '{': {
        int eof, dir = k == '}' ? 1 : -1, ty = findpar(dir, cnt, &eof);
        if (ty == E.cy && (dir < 0 ? E.cx == 0 : (eof && E.cx >= imax(lenof(ty) - 1, 0)))) { m.ok = 0; break; }
        m.y = ty; m.x = 0;
        if (dir > 0 && eof) { m.x = imax(lenof(ty) - 1, 0); m.incl = 1; break; }   /* the end of the text: its last character, taken */
        if (!op) break;
        /* An operator on an exclusive motion that ends in column 0 (:h
         * exclusive-linewise): whole lines if it starts at or before its
         * line's first non-blank, else up to the end of the line before. */
        int sy = dir > 0 ? E.cy : ty, sx = dir > 0 ? E.cx : 0, ey = dir > 0 ? ty : E.cy, ex = dir > 0 ? 0 : E.cx;
        if (ex != 0 || ey == sy) break;
        if (sx <= first_nonblank(&E.t, sy)) { m.line = 1; m.y = dir > 0 ? ey - 1 : sy; if (dir < 0) E.cy = ey - 1; }
        else if (dir > 0) { m.y = ey - 1; m.x = imax(lenof(ey - 1) - 1, 0); m.incl = lenof(ey - 1) > 0; }
        break;
    }
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
    reg_after(op);
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
static int obj_para(int a, int cnt, int *ay, int *by) {   /* ip ap (v0.60.235): whole lines */
    int n = E.t.n, kind = blank(E.cy), s = E.cy, e = E.cy;
    while (s > 0 && blank(s - 1) == kind) s--;
    while (e + 1 < n && blank(e + 1) == kind) e++;
    if (!a) {
        for (int k = 1; k < cnt && e + 1 < n; k++) { int kd = blank(e + 1); e++; while (e + 1 < n && blank(e + 1) == kd) e++; }
    } else if (kind) {                               /* on blank lines: them and the paragraph after */
        for (int k = 0; k < cnt && e + 1 < n; k++) {
            if (k) { e++; while (e + 1 < n && blank(e + 1)) e++; }
            e++; while (e + 1 < n && !blank(e + 1)) e++;
        }
    } else {
        int after = 0;
        for (int k = 0; k < cnt; k++) {
            if (k) { if (e + 1 >= n) break; e++; while (e + 1 < n && !blank(e + 1)) e++; }
            after = 0;
            while (e + 1 < n && blank(e + 1)) { e++; after = 1; }
        }
        if (!after) while (s > 0 && blank(s - 1)) s--;   /* none after: the blank lines before */
    }
    *ay = s; *by = e;
    return 1;
}
static int textobj(int a, int k, int cnt, int *ay, int *ax, int *by, int *bx, int *line) {
    *line = 0; *ay = *by = E.cy;
    switch (k) {
    case 'p': *line = 1; *ax = *bx = 0; return obj_para(a, cnt, ay, by);
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
    /* Before the register, the undo step and c's insert (v0.60.295, review
     * R-N48): asked only inside delete_chars, a refused join still took the
     * register, left an empty undo step and [+], and c went on into insert. */
    if (op != 'y' && by != ay && too_long_join(ax + lenof(by) - bx)) { reg_sel = 0; return; }
    reg_take_chars(ay, ax, by, bx);
    reg_after(op);
    if (op == 'y') { E.cy = ay; E.cx = ax; clamp(); E.want = E.cx; return; }
    change_begin();
    delete_chars(ay, ax, by, bx);
    E.cy = ay; E.cx = ax;
    if (op == 'c') { insert_mode(ax, 0); return; }
    clamp(); E.want = E.cx;
}
static void paste_from(int after, int cnt, const char *reg, int reg_n, int reg_lines);
static void paste(int after, int cnt) {               /* (v0.60.240) from the register named, else the unnamed */
    int b = reg_sel && reg_sel != '"' ? bank_of(reg_sel) : -1;
    reg_sel = 0;
    if (b >= 0) paste_from(after, cnt, bank[b], bank_n[b], bank_lines[b]);
    else paste_from(after, cnt, reg, reg_n, reg_lines);
}
/* The widest line a charwise paste of cnt copies would make (v0.60.305,
 * review R-N54): insert_text skips what text_insert_char refuses, so a paste
 * past 255 was cut with nothing said. One line: this line and every copy.
 * Several: the head and the register's first line, its middle lines, its
 * last and first where two copies meet, and its last and the tail. */
static int too_wide_paste(int after, int cnt, const char *reg, int reg_n) {
    int x = after && len() ? E.cx + 1 : E.cx, head = x, tail = len() - x;
    int first = 0, last = 0, mid = 0, nl = 0, run = 0;
    for (int i = 0; i < reg_n; i++) {
        if (reg[i] != '\n') { run++; continue; }
        if (!nl) first = run; else if (run > mid) mid = run;
        nl++; run = 0;
    }
    last = run;
    long c = cnt > 0 ? cnt : 1, wide;
    if (c > T_COLS + 1) c = T_COLS + 1;              /* already too wide; 99999 * reg_n would wrap a 32-bit long */
    if (!nl) wide = (long)len() + c * reg_n;
    else {
        wide = head + first;
        if (last + tail > wide) wide = last + tail;
        if (mid > wide) wide = mid;
        if (c > 1 && last + first > wide) wide = last + first;
    }
    if (wide <= T_COLS) return 0;
    msg("E1240: Resulting text too long", 0, 0);
    eprint("[neosim] refused a paste past 255\n");
    return 1;
}
static void paste_from(int after, int cnt, const char *reg, int reg_n, int reg_lines) {
    if (!reg_n && !reg_lines) return;
    /* Before the undo step (v0.60.296, review R-N49): refused after it, a
     * paste into a full buffer left an empty step and [+]. */
    /* Every line the paste would make, before anything (v0.60.298, review
     * R-N50): split by split, a nearly full buffer ran the register's lines
     * together into the last, cut at 255, and said nothing. */
    int nl = 0;
    for (int i = 0; i < reg_n; i++) if (reg[i] == '\n') nl++;
    long need = (long)(reg_lines ? nl + 1 : nl) * (cnt > 0 ? cnt : 1);
    if (need && (need > T_MAXL || !text_room_for(&E.t, (int)need))) { msg("E: the file is full (2048 lines): nothing put", 0, 0); return; }
    if (!reg_lines && too_wide_paste(after, cnt, reg, reg_n)) return;
    change_begin();
    if (reg_lines) {
        int y = after ? E.cy + 1 : E.cy, at = y, put = 0;
        for (int c = 0; c < cnt; c++) {
            if (text_insert_line(&E.t, y) < 0) break;
            int yy = y, xx = 0;
            insert_text(&yy, &xx, reg, reg_n);
            y = yy + 1; put++;
        }
        /* Nothing in (v0.60.288, review R-N2): the buffer is full. The cursor
         * went to the line it would have made, one past the last, and the
         * next read of it killed neosim. It stays where it was. */
        if (!put) { msg("E: the file is full (2048 lines): nothing put", 0, 0); return; }
        if (at >= E.t.n) at = E.t.n - 1;
        E.cy = at; E.cx = first_nonblank(&E.t, at); E.want = E.cx;
        return;
    }
    int y = E.cy, x = after && len() ? E.cx + 1 : E.cx;
    for (int c = 0; c < cnt; c++) insert_text(&y, &x, reg, reg_n);
    E.cy = y; E.cx = x - 1; clamp(); E.want = E.cx;
}
/* What J would make of line y and the next: its length (v0.60.295). */
static int join_len(int y) {
    int n2 = y + 1, skip = 0, at = lenof(y);
    while (skip < lenof(n2) && E.t.s[n2][skip] == ' ') skip++;
    int space = at > 0 && skip < lenof(n2) && E.t.s[y][at - 1] != ' ' && E.t.s[n2][skip] != ')';
    return at + space + lenof(n2) - skip;
}
static void join(int cnt) {
    int times = imax(cnt, 2) - 1;
    if (E.cy + 1 >= E.t.n) return;
    if (too_long_join(join_len(E.cy))) return;      /* before the undo step (v0.60.295, review R-N48) */
    change_begin();
    for (int i = 0; i < times && E.cy + 1 < E.t.n; i++) {
        int n2 = E.cy + 1, skip = 0;
        while (skip < lenof(n2) && E.t.s[n2][skip] == ' ') skip++;
        int at = len();
        int space = at > 0 && skip < lenof(n2) && E.t.s[E.cy][at - 1] != ' ' && E.t.s[n2][skip] != ')';
        if (i > 0 && too_long_join(at + space + lenof(n2) - skip)) break;   /* the first asked above */
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
/* Ctrl-A Ctrl-X (v0.60.248), as neovim's (nrformats bin,hex): the number
 * under the cursor or the first after it on the line, a - just before it
 * its sign, 0x numbers hex (their digits' count and case kept); the cursor
 * to its last character. */
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void add_number(int delta) {
    const char *t = E.t.s[E.cy]; int n = len(), p = 0;
    while (p < n) {                                  /* the numbers on the line, in turn */
        if (!(t[p] >= '0' && t[p] <= '9')) { p++; continue; }
        int hex = t[p] == '0' && p + 2 < n && (t[p + 1] == 'x' || t[p + 1] == 'X') && hexv(t[p + 2]) >= 0;
        int d0 = hex ? p + 2 : p, e = d0;
        while (e < n && (hex ? hexv(t[e]) >= 0 : t[e] >= '0' && t[e] <= '9')) e++;
        int s0 = !hex && p > 0 && t[p - 1] == '-' ? p - 1 : p;
        if (e <= E.cx) { p = e; continue; }          /* before the cursor */
        /* A whole line's worth (v0.60.289, review R-N31): a hex number is
         * zero-padded to as many digits as it had, up to 253, and out[40] let
         * a 61-digit one write past it and take neosim down. The fit check
         * below still refuses a result the line has no room for. */
        char out[T_COLS + 8]; int k = 0;
        if (hex) {
            unsigned v = 0; int upper = 0;
            for (int i = d0; i < e; i++) { v = v * 16 + (unsigned)hexv(t[i]); if (t[i] >= 'A' && t[i] <= 'F') upper = 1; }
            v += (unsigned)delta;
            char d[12]; int m = 0;
            do { int h = (int)(v & 15); d[m++] = (char)(h < 10 ? '0' + h : (upper ? 'A' : 'a') + h - 10); v >>= 4; } while (v);
            out[k++] = t[p]; out[k++] = t[p + 1];
            for (int i = m; i < e - d0; i++) out[k++] = '0';   /* as many digits as it had */
            while (m) out[k++] = d[--m];
        } else {
            long long v = 0;
            for (int i = p; i < e; i++) v = v * 10 + (t[i] - '0');
            if (s0 < p) v = -v;
            v += delta;
            if (v < 0) { out[k++] = '-'; v = -v; }
            char d[24]; int m = 0;
            do { d[m++] = (char)('0' + v % 10); v /= 10; } while (v);
            while (m) out[k++] = d[--m];
        }
        if (n - (e - s0) + k > T_COLS) return;
        change_begin();
        for (int i = s0; i < e; i++) text_delete_char(&E.t, E.cy, s0);
        for (int i = 0; i < k; i++) text_insert_char(&E.t, E.cy, s0 + i, out[i]);
        E.cx = s0 + k - 1; E.want = E.cx;
        return;
    }
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
        if (w == 'q') {                              /* (v0.60.241) start recording */
            if (bank_of((int)k) >= 0) { rec_reg = (int)k; rec_n = 0; E.recording = (char)k; }
            return;
        }
        if (w == '@') { macro_play(k == '@' ? last_play : (int)k, cnt); return; }
        if (w == '"') {                              /* (v0.60.240) "x: the register for what comes next */
            if (bank_of((int)k) >= 0 || k == '"') { reg_sel = (int)k; reg_fresh = 1; }
            if (had) P.count = cnt;
            return;
        }
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
            || (k == 'm' && !P.op) || k == '\'' || k == '`' || (k == '"' && !P.op) || ((k == 'q' || k == '@') && !P.op)) { P.wait = (int)k; P.wcount = cnt; P.whad = had; return; }
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
    case 0x01: add_number(cnt); break;               /* Ctrl-A (v0.60.248) */
    case 0x18: add_number(-cnt); break;              /* Ctrl-X */
    case 'u': { int n = 0; for (int i = 0; i < cnt; i++) n += undo_undo(&E.t, &E.cy, &E.cx, &E.seq); after_undo(); if (!n) msg("Already at oldest change", 0, 0); break; }
    case 0x12: { int n = 0; for (int i = 0; i < cnt; i++) n += undo_redo(&E.t, &E.cy, &E.cx, &E.seq); after_undo(); if (!n) msg("Already at newest change", 0, 0); break; }
    case 'i': insert_mode(E.cx, 1); ins_count = cnt; break;
    case 'a': insert_mode(len() ? E.cx + 1 : 0, 1); ins_count = cnt; break;
    case 'A': insert_mode(len(), 1); ins_count = cnt; break;
    case 'I': insert_mode(len() ? first_nonblank(&E.t, E.cy) : 0, 1); ins_count = cnt; break;
    /* Room for every line the count makes (v0.60.304, review R-N53): asked
     * for one, a 3o on a nearly full file split what it could and ran the
     * rest together on the last line. */
    case 'o': { if (!text_room_for(&E.t, cnt)) { full_msg(); break; }   /* (v0.60.296) */
                int ind = indent_of(E.cy); change_begin(); if (text_insert_line(&E.t, E.cy + 1) == 0) { E.cy++; put_indent(E.cy, ind); insert_mode(ind, 0); ai_pending = ind; ins_count = cnt; ins_open = 1; } break; }
    case 'O': { if (!text_room_for(&E.t, cnt)) { full_msg(); break; }
                int ind = indent_of(E.cy); change_begin(); if (text_insert_line(&E.t, E.cy) == 0) { put_indent(E.cy, ind); insert_mode(ind, 0); ai_pending = ind; ins_count = cnt; ins_open = 1; } break; }
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
    int last = E.pagen > TEXT_ROWS ? E.pagen - TEXT_ROWS : 0;
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
/* Macros (v0.60.241): q and a letter records the keys typed until q (in
 * normal mode), into that register as text; @ and a letter plays it, a
 * count that many times, @@ the last one again; one undo step for it all. */
static void macro_play(int r, int cnt) {
    int b = bank_of(r);
    if (b < 0 || play_depth > 8) return;
    static char run[9][BANK_MAX];                    /* a copy: the macro may change its own register */
    int n = bank_n[b];
    for (int i = 0; i < n; i++) run[play_depth][i] = bank[b][i];
    int outer = !grouping;
    if (outer) { grouping = 1; group_pushed = 0; }
    char *m = run[play_depth++];
    last_play = r;
    for (int c = 0; c < cnt; c++) for (int i = 0; i < n; i++) key((unsigned)(unsigned char)m[i]);
    play_depth--;
    if (outer) grouping = 0;
    cur_change_hook = 0; cur_change = 0;              /* . is the macro's last change, not @a */
}
static int key(unsigned k) {                        /* 1: end neosim */
    if (rec_reg && !play_depth && !replaying) {      /* recording */
        if (k == 'q' && E.mode == M_NORMAL && idle()) {
            int b = bank_of(rec_reg);
            if (rec_reg >= 'A' && rec_reg <= 'Z') { for (int i = 0; i < rec_n && bank_n[b] < BANK_MAX; i++) bank[b][bank_n[b]++] = rec_buf[i]; }
            else bank_put(b, rec_buf, rec_n, 0);
            rec_reg = 0; E.recording = 0; E.msg[0] = 0;
            return 0;
        }
        if (k < 256 && rec_n < BANK_MAX) rec_buf[rec_n++] = (char)k;
    }
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
    if (idle()) { if (reg_fresh) reg_fresh = 0; else reg_sel = 0; }   /* (v0.60.240) a register named lasts one command */
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
    E.nu = 1;                                        /* numbers on, as neosim always had them (neovim's are off) */
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

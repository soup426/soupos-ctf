/* main.c - neosim (v0.60.204): a modal editor after neovim, in a window of
 * its own on the desktop. The user's name for it: it simmers.
 *
 *   cook neosim.elf FILE          (from a terminal on the desktop)
 *   cook neosim.elf -wait FILE &  wait up to a minute for the desktop, for
 *                                 tests that start it at the console
 *
 * Normal mode: h j k l and the arrows, i a A I o O into insert mode, x,
 * : for a command. Insert mode: typing, Enter, Backspace (joining lines at
 * the start of one), the arrows, Esc back. Commands: :w [FILE], :q, :q!,
 * :wq, :x, :e[!] FILE. :q with changes is refused, as vim's E37.
 * The close box is :q (refused with changes, said so).
 *
 * Markers on stderr, [neosim] ...: open PATH lines=N (or new PATH), wrote
 * PATH bytes=N, refused :q (changed), bye.
 */
#include "../ulib.h"
#include "ed.h"

static ed_t E;

static void eprint_u(unsigned v) {
    char b[12]; int n = 11; b[n] = 0;
    do { b[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    eprint(b + n);
}
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scopy(char *d, const char *s, int n) { int i = 0; while (s[i] && i < n - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void msg(const char *a, const char *b, const char *c) {
    E.msg[0] = 0;
    int n = 0;
    for (const char *p = a; p && *p && n < 79; p++) E.msg[n++] = *p;
    for (const char *p = b; p && *p && n < 79; p++) E.msg[n++] = *p;
    for (const char *p = c; p && *p && n < 79; p++) E.msg[n++] = *p;
    E.msg[n] = 0;
}
static int len(void) { return E.t.len[E.cy]; }
static void clamp(void) {                           /* the cursor inside the line (on its last character in normal mode) */
    int max = E.mode == M_INSERT ? len() : (len() > 0 ? len() - 1 : 0);
    if (E.cx > max) E.cx = max;
    if (E.cx < 0) E.cx = 0;
}
static void vertical(int d) {
    int y = E.cy + d;
    if (y < 0 || y >= E.t.n) return;
    E.cy = y; E.cx = E.want; clamp();
}
static void horizontal(int x) { E.cx = x; clamp(); E.want = E.cx; }
static void scroll(void) {
    if (E.cy < E.top) E.top = E.cy;
    if (E.cy >= E.top + TEXT_ROWS) E.top = E.cy - TEXT_ROWS + 1;
    int w = SCR_COLS - NUM_W;
    if (E.cx < E.left) E.left = E.cx;
    if (E.cx >= E.left + w) E.left = E.cx - w + 1;
}
static void open_file(const char *path) {
    scopy(E.path, path, sizeof(E.path));
    int r = text_load(&E.t, path);
    E.cy = E.cx = E.want = E.top = E.left = 0; E.changed = 0;
    if (r == -1) { eprint("[neosim] new "); eprint(path); eprint("\n"); msg("\"", path, "\" [New]"); return; }
    eprint("[neosim] open "); eprint(path); eprint(" lines="); eprint_u((unsigned)E.t.n); eprint("\n");
    char n[12]; int k = 0; unsigned v = (unsigned)E.t.n; char d[12]; int m = 0;
    do { d[m++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (m) n[k++] = d[--m];
    n[k] = 0;
    msg("\"", path, r == -2 ? "\" too long: only the first lines" : E.t.cut ? "\" (long lines cut at 255)" : "\"");
    if (r == 0 && !E.t.cut) { msg("\"", path, "\" "); int l = 0; while (E.msg[l]) l++; for (int i = 0; n[i] && l < 70; i++) E.msg[l++] = n[i]; for (const char *s = "L"; *s; s++) E.msg[l++] = *s; E.msg[l] = 0; }
}
static int write_file(const char *path) {          /* 0, or -1 */
    unsigned bytes = 0;
    if (!path[0]) { msg("E32: No file name", 0, 0); return -1; }
    if (text_save(&E.t, path, &bytes) < 0) { msg("E212: Can't open file for writing: ", path, 0); eprint("[neosim] write failed\n"); return -1; }
    eprint("[neosim] wrote "); eprint(path); eprint(" bytes="); eprint_u(bytes); eprint("\n");
    if (path != E.path) scopy(E.path, path, sizeof(E.path));
    E.changed = 0;
    char b[12]; int n = 11; b[n] = 0; unsigned v = bytes;
    do { b[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    msg("\"", path, "\" written, "); int l = 0; while (E.msg[l]) l++;
    for (const char *s = b + n; *s && l < 70; s++) E.msg[l++] = *s;
    for (const char *s = "B"; *s; s++) E.msg[l++] = *s;
    E.msg[l] = 0;
    return 0;
}
/* A command after :. Returns 1 to end neosim. */
static int command(char *c) {
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
static void insert_mode(int x) { E.mode = M_INSERT; E.cx = x; clamp(); E.want = E.cx; }
/* One key. Returns 1 to end neosim. */
static int key(unsigned k) {
    if (E.mode == M_COMMAND) {
        if (k == 27) { E.mode = M_NORMAL; return 0; }
        if (k == '\n') { E.cmd[E.cmdn] = 0; E.mode = M_NORMAL; E.msg[0] = 0; return command(E.cmd); }
        if (k == '\b') { if (E.cmdn) E.cmdn--; else E.mode = M_NORMAL; return 0; }
        if (k >= ' ' && k < 127 && E.cmdn < (int)sizeof(E.cmd) - 1) E.cmd[E.cmdn++] = (char)k;
        return 0;
    }
    E.msg[0] = 0;
    if (k == KEY_LEFT) { horizontal(E.cx - 1); return 0; }
    if (k == KEY_RIGHT) { horizontal(E.cx + 1); return 0; }
    if (k == KEY_UP) { vertical(-1); return 0; }
    if (k == KEY_DOWN) { vertical(1); return 0; }
    if (E.mode == M_INSERT) {
        if (k == 27) { E.mode = M_NORMAL; horizontal(E.cx - 1); return 0; }
        if (k == '\n') { if (text_split(&E.t, E.cy, E.cx) == 0) { E.cy++; E.cx = E.want = 0; E.changed = 1; } return 0; }
        if (k == '\b') {
            if (E.cx > 0) { text_delete_char(&E.t, E.cy, E.cx - 1); E.cx--; E.want = E.cx; E.changed = 1; }
            else if (E.cy > 0) { int at = E.t.len[E.cy - 1]; text_join(&E.t, E.cy - 1); E.cy--; E.cx = E.want = at; E.changed = 1; }
            return 0;
        }
        if (k == '\t') { for (int i = 0; i < 4; i++) if (text_insert_char(&E.t, E.cy, E.cx, ' ') == 0) E.cx++; E.want = E.cx; E.changed = 1; return 0; }
        if (k >= ' ' && k < 127) {
            if (text_insert_char(&E.t, E.cy, E.cx, (char)k) == 0) { E.cx++; E.want = E.cx; E.changed = 1; }
            else msg("the line is full (255)", 0, 0);
        }
        return 0;
    }
    switch (k) {                                    /* normal mode */
    case 'h': horizontal(E.cx - 1); break;
    case 'l': horizontal(E.cx + 1); break;
    case 'j': vertical(1); break;
    case 'k': vertical(-1); break;
    case 'i': insert_mode(E.cx); break;
    case 'a': insert_mode(len() ? E.cx + 1 : 0); break;
    case 'A': insert_mode(len()); break;
    case 'I': { int x = 0; while (x < len() && E.t.s[E.cy][x] == ' ') x++; insert_mode(x); break; }
    case 'o': if (text_insert_line(&E.t, E.cy + 1) == 0) { E.cy++; E.changed = 1; insert_mode(0); } break;
    case 'O': if (text_insert_line(&E.t, E.cy) == 0) { E.changed = 1; insert_mode(0); } break;
    case 'x': if (len()) { text_delete_char(&E.t, E.cy, E.cx); E.changed = 1; clamp(); E.want = E.cx; } break;
    case ':': E.mode = M_COMMAND; E.cmdn = 0; break;
    }
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
    if (text_init() < 0) { print("neosim: no memory\n"); return 1; }
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
        int g = sys_win_event(win, &ev, 1000);              /* waits: nothing to do until a key */
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

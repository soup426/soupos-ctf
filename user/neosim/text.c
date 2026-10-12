/* text.c - neosim's buffer (v0.60.204). */
#include "../ulib.h"
#include "text.h"

static char *pool;
static char *freel[T_MAXL];
static int nfree;

int text_init(void) {
    void *p = sys_sbrk(T_MAXL * (T_COLS + 1));
    if (p == (void *)-1) return -1;
    pool = (char *)p;
    for (int i = 0; i < T_MAXL; i++) freel[i] = pool + (T_MAXL - 1 - i) * (T_COLS + 1);
    nfree = T_MAXL;
    return 0;
}
static char *blk(void) { return nfree ? freel[--nfree] : 0; }
static void unblk(char *b) { if (b) freel[nfree++] = b; }

void text_clear(text_t *t) {
    for (int i = 0; i < t->n; i++) unblk(t->s[i]);
    t->n = 1; t->s[0] = blk(); t->len[0] = 0; t->cut = 0;
}
int text_load(text_t *t, const char *path) {
    text_clear(t);
    int fd = sys_open(path, O_READ);
    if (fd < 0) return -1;
    static char buf[4096];
    int got, line = 0, over = 0, full = 0;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < got; i++) {
            char c = buf[i];
            if (c == '\n') {
                if (t->n == T_MAXL) { full = 1; break; }
                if (over) t->cut++;
                over = 0;
                line = t->n++;
                t->s[line] = blk(); t->len[line] = 0;
                continue;
            }
            if (c == '\r') continue;
            if (c == '\t') c = ' ';
            if (t->len[line] < T_COLS) t->s[line][t->len[line]++] = c;
            else over = 1;
        }
        if (full) break;
    }
    sys_close(fd);
    if (over) t->cut++;
    /* A last newline ends the last line rather than starting another. */
    if (t->n > 1 && t->len[t->n - 1] == 0) { unblk(t->s[t->n - 1]); t->n--; }
    return full ? -2 : 0;
}
int text_save(const text_t *t, const char *path, unsigned *bytes) {
    int fd = sys_open(path, O_WRITE);
    if (fd < 0) return -1;
    static char buf[4096];
    int n = 0, bad = 0;
    unsigned total = 0;
    for (int i = 0; i < t->n; i++) {
        for (int k = 0; k <= t->len[i]; k++) {
            buf[n++] = k < t->len[i] ? t->s[i][k] : '\n';
            if (n == (int)sizeof(buf)) { if (sys_write(fd, buf, n) != n) bad = 1; total += (unsigned)n; n = 0; }
        }
    }
    if (n && sys_write(fd, buf, n) != n) bad = 1;
    total += (unsigned)n;
    if (sys_close(fd) < 0) bad = 1;                     /* the write is at the close */
    if (bytes) *bytes = total;
    return bad ? -1 : 0;
}
/* (v0.60.231) told of every line put in or taken out, so marks can move */
void (*text_lines_hook)(int at, int delta, int join_col);
static int joining = -1;                         /* text_join: the column line + 1 lands at */
int text_insert_line(text_t *t, int at) {
    if (t->n == T_MAXL || !nfree) return -1;
    for (int i = t->n; i > at; i--) { t->s[i] = t->s[i - 1]; t->len[i] = t->len[i - 1]; }
    t->s[at] = blk(); t->len[at] = 0; t->n++;
    if (text_lines_hook) text_lines_hook(at, 1, -1);
    return 0;
}
void text_delete_line(text_t *t, int at) {
    if (t->n == 1) { t->len[0] = 0; return; }
    unblk(t->s[at]);
    for (int i = at; i < t->n - 1; i++) { t->s[i] = t->s[i + 1]; t->len[i] = t->len[i + 1]; }
    t->n--;
    if (text_lines_hook) text_lines_hook(at, -1, joining);
}
int text_insert_char(text_t *t, int line, int col, char c) {
    if (t->len[line] >= T_COLS) return -1;
    char *s = t->s[line];
    for (int i = t->len[line]; i > col; i--) s[i] = s[i - 1];
    s[col] = c; t->len[line]++;
    return 0;
}
void text_delete_char(text_t *t, int line, int col) {
    char *s = t->s[line];
    if (col < 0 || col >= t->len[line]) return;
    for (int i = col; i < t->len[line] - 1; i++) s[i] = s[i + 1];
    t->len[line]--;
}
int text_split(text_t *t, int line, int col) {
    if (text_insert_line(t, line + 1) < 0) return -1;
    int rest = t->len[line] - col;
    for (int i = 0; i < rest; i++) t->s[line + 1][i] = t->s[line][col + i];
    t->len[line + 1] = rest; t->len[line] = col;
    return 0;
}
void text_join(text_t *t, int line) {
    if (line + 1 >= t->n) return;
    int room = T_COLS - t->len[line], take = t->len[line + 1] < room ? t->len[line + 1] : room;
    joining = t->len[line];
    for (int i = 0; i < take; i++) t->s[line][t->len[line] + i] = t->s[line + 1][i];
    t->len[line] += take;
    text_delete_line(t, line + 1);
    joining = -1;
}

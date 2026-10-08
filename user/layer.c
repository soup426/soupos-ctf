/* layer.c - files side by side, line by line (v0.60.16).
 *
 *   cook layer.elf A.TXT B.TXT          a line of each, a tab between
 *   cook layer.elf -d , A.TXT B.TXT     a comma instead
 *   spoon.elf A.TXT | layer.elf - B.TXT  - is stdin
 *
 * As `paste` does it: the Nth line of every file on one line, the
 * delimiters taken in turn from the -d list (starting over on each line;
 * \t \n \\ and \0, which is none, as escapes), a file that has run out
 * giving an empty field, until every file has. A last line with no
 * newline gets one.
 */
#include "ulib.h"

#define FILES_MAX 8

typedef struct { int fd; char buf[2048]; int pos, len, eof; } src_t;
static src_t src[FILES_MAX];

static char ob[4096];
static int  on;
static void put(char c) {
    if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
    ob[on++] = c;
}

/* Has `s` run out? Reads ahead to find out, so a round with nothing left
 * can be known before any of it is written. */
static int done(src_t *s) {
    if (s->pos == s->len && !s->eof) {
        s->len = sys_read(s->fd, s->buf, sizeof(s->buf));
        s->pos = 0;
        if (s->len <= 0) { s->len = 0; s->eof = 1; }
    }
    return s->pos == s->len && s->eof;
}

/* Copy one line of `s` to the output, without its newline. 0 if it had run out. */
static int copy_line(src_t *s) {
    int any = 0;
    for (;;) {
        if (s->pos == s->len) {
            if (s->eof) return any;
            s->len = sys_read(s->fd, s->buf, sizeof(s->buf));
            s->pos = 0;
            if (s->len <= 0) { s->len = 0; s->eof = 1; return any; }
        }
        char c = s->buf[s->pos++];
        any = 1;
        if (c == '\n') return 1;
        put(c);
    }
}

int main(void) {
    char *tok[FILES_MAX + 4];
    int nt = uargv(tok, FILES_MAX + 4);
    char delim[32]; int nd = 1, t = 0;
    delim[0] = '\t';
    if (t < nt && tok[t][0] == '-' && tok[t][1] == 'd') {
        const char *d = tok[t][2] ? tok[t] + 2 : (t + 1 < nt ? tok[++t] : 0);
        if (!d || !*d) { print("usage: layer.elf [-d LIST] file...\n"); return 2; }
        nd = 0;
        for (; *d && nd < (int)sizeof(delim); d++) {
            char c = *d;
            if (c == '\\' && d[1]) {
                d++;
                c = *d == 't' ? '\t' : *d == 'n' ? '\n' : *d == '0' ? 0 : *d;
            }
            delim[nd++] = c;
        }
        t++;
    }
    int nf = 0;
    for (; t < nt; t++) {
        if (nf == FILES_MAX) { print("layer: at most 8 files\n"); return 2; }
        if (tok[t][0] == '-' && !tok[t][1]) src[nf].fd = FD_STDIN;
        else if ((src[nf].fd = sys_open(tok[t], O_READ)) < 0) {
            print("layer: cannot open "); print(tok[t]); print("\n"); return 1;
        }
        nf++;
    }
    if (nf == 0) { print("usage: layer.elf [-d LIST] file...\n"); return 2; }
    for (;;) {
        int left = 0;
        for (int i = 0; i < nf; i++) if (!done(&src[i])) left = 1;
        if (!left) break;                    /* every file has run out: no line */
        for (int i = 0; i < nf; i++) {
            copy_line(&src[i]);
            if (i < nf - 1 && delim[i % nd]) put(delim[i % nd]);
        }
        put('\n');
    }
    for (int i = 0; i < nf; i++) if (src[i].fd != FD_STDIN) sys_close(src[i].fd);
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

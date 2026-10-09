/* slice.c - fields of each line (v0.56.8).
 *
 *   cook slice.elf -d : -f 1 PASSWD.TXT         the first field of each line
 *   cook slice.elf -d , -f 2,4- DATA.CSV        field 2, then 4 to the end
 *   cook spoon.elf X | slice.elf -f 3             tab-separated, a pipe stage
 *
 * GNU cut's rules for -f: the delimiter is one character (a tab unless
 * -d says otherwise); the list takes N, N-M, N- and -M separated by
 * commas, and fields come out in the line's order, each once, whatever
 * order the list names them; a line with no delimiter is printed whole;
 * a last line with no newline gets one.
 *
 * -c LIST (and -b LIST, the same here: a character is a byte) picks
 * characters by position with the same lists; -s, with -f, leaves out a
 * line with no delimiter instead of printing it whole (v0.60.127).
 */
#include "ulib.h"

#define MAXF 4096                    /* fields, or characters with -c */
static char want[MAXF + 1];          /* want[i]: field i (1-based) selected */
static int  from_open;               /* every field from here on (N-), 0 = none */

static int num(const char **p) {
    int v = 0, any = 0;
    while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; any = 1; }
    return any ? v : -1;
}
static int parse_list(const char *s) {
    while (*s) {
        int a = 1, b;
        if (*s == '-') { s++; b = num(&s); if (b < 1) return -1; }
        else {
            a = num(&s); if (a < 1) return -1;
            if (*s == '-') {
                s++;
                if (*s == ',' || !*s) { if (!from_open || a < from_open) from_open = a; b = a - 1; }
                else { b = num(&s); if (b < a) return -1; }
            } else b = a;
        }
        for (int i = a; i <= b && i <= MAXF; i++) want[i] = 1;
        if (*s == ',') s++;
        else if (*s) return -1;
    }
    return 0;
}
static int selected(int f) { return (f <= MAXF && want[f]) || (from_open && f >= from_open); }

static char *line; static int len, cap;
static void put(char c) {
    if (len == cap) {
        int nc = cap ? cap * 2 : 256;
        char *n = malloc((unsigned)nc);
        for (int i = 0; i < len; i++) n[i] = line[i];
        line = n; cap = nc;
    }
    line[len++] = c;
}

static int chars, only_delimited;    /* -c / -b, -s (v0.60.127) */
static char outb[1024]; static int on;
static void emit(char delim) {
    if (chars) {                      /* the characters at those places, in order */
        on = 0;
        for (int i = 0; i < len; i++)
            if (selected(i + 1)) { outb[on++] = line[i]; if (on == (int)sizeof(outb)) { sys_write(FD_STDOUT, outb, on); on = 0; } }
        if (on) sys_write(FD_STDOUT, outb, on);
        sys_write(FD_STDOUT, "\n", 1);
        return;
    }
    int has = 0;
    for (int i = 0; i < len; i++) if (line[i] == delim) { has = 1; break; }
    if (!has) { if (only_delimited) return; sys_write(FD_STDOUT, line, len); sys_write(FD_STDOUT, "\n", 1); return; }
    int field = 1, start = 0, first = 1;
    for (int i = 0; i <= len; i++) {
        if (i < len && line[i] != delim) continue;
        if (selected(field)) {
            if (!first) sys_write(FD_STDOUT, &delim, 1);
            sys_write(FD_STDOUT, line + start, i - start);
            first = 0;
        }
        field++; start = i + 1;
    }
    sys_write(FD_STDOUT, "\n", 1);
}

int main(void) {
    char *tok[8];
    int nt = uargv(tok, 8);        /* sh's splitting, quotes kept together (v0.57.2) */
    char delim = '\t';
    int have_list = 0, t = 0;
    while (t < nt && tok[t][0] == '-' && tok[t][1]) {
        char o = tok[t][1];
        if (o == 's' && !tok[t][2]) { only_delimited = 1; t++; continue; }
        const char *val = tok[t][2] ? tok[t] + 2 : (t + 1 < nt ? tok[t + 1] : 0);
        if ((o != 'd' && o != 'f' && o != 'c' && o != 'b') || !val) { print("usage: slice.elf [-d C] [-s] -f LIST | -c LIST [file]\n"); return 2; }
        if (o == 'd') {
            if (val[0] == 0 || val[1] != 0) { print("slice: the delimiter must be one character\n"); return 2; }
            delim = val[0];
        } else {
            if (have_list) { print("slice: only one of -f, -c, -b\n"); return 2; }
            if (parse_list(val) < 0) { print("slice: bad list\n"); return 2; }
            have_list = 1;
            chars = o != 'f';
        }
        t += tok[t][2] ? 1 : 2;
    }
    if (!have_list) { print("usage: slice.elf [-d C] [-s] -f LIST | -c LIST [file]\n"); return 2; }
    int fd = FD_STDIN;
    if (t < nt) {
        fd = sys_open(tok[t], O_READ);
        if (fd < 0) { print("slice: cannot open "); print(tok[t]); print("\n"); return 1; }
    }
    char buf[512];
    int got, partial = 0;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0)
        for (int i = 0; i < got; i++) {
            if (buf[i] == '\n') { emit(delim); len = 0; partial = 0; }
            else { put(buf[i]); partial = 1; }
        }
    if (partial) emit(delim);
    if (fd != FD_STDIN) sys_close(fd);
    return 0;
}

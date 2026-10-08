/* weigh.c - count lines, words and bytes, in the host's format (v0.56.6).
 *
 *   cook weigh.elf RECIPE.TXT            " 25  50 191 RECIPE.TXT"
 *   cook weigh.elf -l RECIPE.TXT         "25 RECIPE.TXT"
 *   cook call.elf hello | weigh.elf -w   "1"
 *
 * GNU wc's format, measured on the host: one count is printed bare; more
 * than one are each right-aligned to the width of the byte count, one
 * space apart; a file argument's name follows. One difference: GNU uses a
 * width of 7 when its input is a pipe, and weigh.elf cannot tell a pipe from
 * a redirected file (there is no fstat), so it always uses the file rule.
 *
 * Also emits the counts to serial as a WCOUT marker, so the headless smoke
 * test can read the result of a pipeline rather than infer it.
 */
#include "ulib.h"

static int digits(int v) { int d = 1; while (v >= 10) { v /= 10; d++; } return d; }
static void put_num(int v, int width) {
    char d[12]; int k = 0;
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = k; i < width; i++) sys_write(FD_STDOUT, " ", 1);
    while (k) sys_write(FD_STDOUT, &d[--k], 1);
}

int main(void) {
    char *tok[6];
    int nt = uargv(tok, 6);        /* sh's splitting, quotes kept together (v0.57.2) */
    int want_l = 0, want_w = 0, want_c = 0, t = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1]; t++)
        for (char *f = tok[t] + 1; *f; f++) {
            if      (*f == 'l') want_l = 1;
            else if (*f == 'w') want_w = 1;
            else if (*f == 'c') want_c = 1;
            else { print("usage: weigh.elf [-l] [-w] [-c] [file]\n"); return 2; }
        }
    if (!want_l && !want_w && !want_c) want_l = want_w = want_c = 1;
    const char *name = 0;
    int fd = FD_STDIN;
    if (t < nt) {
        name = tok[t];
        fd = sys_open(name, O_READ);
        if (fd < 0) { print("weigh: cannot open "); print(name); print("\n"); return 1; }
    }

    char buf[256];
    int got, lines = 0, words = 0, bytes = 0, in_word = 0;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < got; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n') lines++;
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f') in_word = 0;
            else if (!in_word) { in_word = 1; words++; }
        }
    }
    if (fd != FD_STDIN) sys_close(fd);

    int shown = want_l + want_w + want_c;
    int width = shown > 1 ? digits(bytes) : 0;
    int first = 1;
    if (want_l) { put_num(lines, width); first = 0; }
    if (want_w) { if (!first) sys_write(FD_STDOUT, " ", 1); put_num(words, width); first = 0; }
    if (want_c) { if (!first) sys_write(FD_STDOUT, " ", 1); put_num(bytes, width); }
    if (name) { sys_write(FD_STDOUT, " ", 1); print(name); }
    sys_write(FD_STDOUT, "\n", 1);

    eprint("WCOUT lines=");  eprint_int(lines);
    eprint(" words=");       eprint_int(words);
    eprint(" bytes=");       eprint_int(bytes);
    eprint("\n");
    return 0;
}

/* pair.c - compare two files byte by byte (v0.59.4).
 *
 *   cook pair.elf A.TXT B.TXT && cook call.elf same
 *
 * GNU cmp's messages and codes in the C locale, as measured on the host
 * (in a UTF-8 locale it says "byte" for "char" and uses curly quotes):
 *   the same                  exit 0, nothing printed
 *   they differ               "A B differ: char N, line M" on stdout, exit 1
 *   one ends first            "pair: EOF on 'A' after byte N, line M" when
 *                             the common part ends a line, "..., in line M"
 *                             inside one, "... which is empty" if empty;
 *                             on stderr, exit 1
 *   a file cannot be opened   "pair: A: No such file or directory", exit 2
 * Bytes and lines count from 1, the line being the one the difference is on.
 *
 * -l and -s (v0.60.139), as GNU's: -l every differing byte, its number
 * right-aligned to the digits of the smaller file's size and both values
 * in octal (" %3o %3o"), and an EOF said without its line; -s nothing said at
 * all, only the status; -l with -s is a usage error, 2. Status 1 whenever a
 * byte differed: GNU cmp 3.12 -l gives 0 when the last buffer it compares
 * of a file over ~16 KB has no difference, which is its bug, not copied.
 */
#include "ulib.h"

static void put_num(int fd, unsigned v) {
    char d[12]; int k = 0; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) { char c = d[--k]; sys_write(fd, &c, 1); }
}
static void out(int fd, const char *s) { sys_write(fd, s, strlen_(s)); }

static void put_pad(unsigned v, int width) {
    char d[12]; int k = 0; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = k; i < width; i++) sys_write(FD_STDOUT, " ", 1);
    while (k) { char c = d[--k]; sys_write(FD_STDOUT, &c, 1); }
}
static void put_oct(unsigned char v) {
    char o[4] = { ' ', ' ', ' ', 0 }; int k = 2;
    do { o[k--] = (char)('0' + (v & 7)); v >>= 3; } while (v && k >= 0);
    sys_write(FD_STDOUT, o, 3);
}

int main(void) {
    char *argbuf[6], **argv = argbuf;
    int argc = uargv(argbuf, 6), a0 = 0, list = 0, silent = 0;
    for (; a0 < argc && argv[a0][0] == '-' && argv[a0][1]; a0++)
        for (const char *f = argv[a0] + 1; *f; f++) {
            if (*f == 'l') list = 1;
            else if (*f == 's') silent = 1;
            else { out(FD_STDERR, "usage: pair.elf [-l | -s] FILE1 FILE2\n"); return 2; }
        }
    if (list && silent) { out(FD_STDERR, "pair: options -l and -s are incompatible\n"); return 2; }
    if (argc - a0 != 2) { out(FD_STDERR, "usage: pair.elf [-l | -s] FILE1 FILE2\n"); return 2; }
    argv += a0;
    int fa = sys_open(argv[0], O_READ);
    if (fa < 0) { if (!silent) { out(FD_STDERR, "pair: "); out(FD_STDERR, argv[0]); out(FD_STDERR, ": No such file or directory\n"); } return 2; }
    int fb = sys_open(argv[1], O_READ);
    if (fb < 0) { if (!silent) { out(FD_STDERR, "pair: "); out(FD_STDERR, argv[1]); out(FD_STDERR, ": No such file or directory\n"); } sys_close(fa); return 2; }
    int width = 1;
    if (list) {                                        /* the smaller file's size, in digits (as far as they can differ) */
        ustat_t sa, sb; unsigned small = 0;
        if (sys_stat(argv[0], &sa) == 0 && sys_stat(argv[1], &sb) == 0) small = sa.size < sb.size ? sa.size : sb.size;
        for (unsigned v = small; v >= 10; v /= 10) width++;
    }
    char ba[512], bb[512];
    int na = 0, nb = 0, ia = 0, ib = 0;
    unsigned byte = 0, lines = 0;      /* bytes and newlines matched so far */
    int last_nl = 1;
    int rc = 0;
    for (;;) {
        if (ia == na) { na = sys_read(fa, ba, sizeof(ba)); ia = 0; if (na < 0) na = 0; }
        if (ib == nb) { nb = sys_read(fb, bb, sizeof(bb)); ib = 0; if (nb < 0) nb = 0; }
        int ea = (na == 0), eb = (nb == 0);
        if (ea && eb) break;                                  /* the same */
        if (ea || eb) {
            const char *who = ea ? argv[0] : argv[1];
            if (silent) { rc = 1; break; }
            out(FD_STDERR, "pair: EOF on '"); out(FD_STDERR, who); out(FD_STDERR, "'");
            if (byte == 0) out(FD_STDERR, " which is empty\n");
            else if (list) { out(FD_STDERR, " after byte "); put_num(FD_STDERR, byte); out(FD_STDERR, "\n"); }
            else {
                out(FD_STDERR, " after byte "); put_num(FD_STDERR, byte);
                if (last_nl) { out(FD_STDERR, ", line "); put_num(FD_STDERR, lines); }
                else         { out(FD_STDERR, ", in line "); put_num(FD_STDERR, lines + 1); }
                out(FD_STDERR, "\n");
            }
            rc = 1; break;
        }
        char x = ba[ia++], y = bb[ib++];
        if (x != y && list) {                          /* -l: each one, then on */
            put_pad(byte + 1, width); sys_write(FD_STDOUT, " ", 1); put_oct((unsigned char)x);
            sys_write(FD_STDOUT, " ", 1); put_oct((unsigned char)y);
            sys_write(FD_STDOUT, "\n", 1);
            rc = 1;
        } else if (x != y) {
            if (silent) { rc = 1; break; }
            out(FD_STDOUT, argv[0]); out(FD_STDOUT, " "); out(FD_STDOUT, argv[1]);
            out(FD_STDOUT, " differ: char "); put_num(FD_STDOUT, byte + 1);
            out(FD_STDOUT, ", line "); put_num(FD_STDOUT, lines + 1);
            out(FD_STDOUT, "\n");
            rc = 1; break;
        }
        byte++;
        if (x == '\n') lines++;
        last_nl = (x == '\n');
    }
    sys_close(fa); sys_close(fb);
    return rc;
}

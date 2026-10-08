/* cmp.c - compare two files byte by byte (v0.59.4).
 *
 *   cook cmp.elf A.TXT B.TXT && cook echo.elf same
 *
 * GNU cmp's messages and codes in the C locale, as measured on the host
 * (in a UTF-8 locale it says "byte" for "char" and uses curly quotes):
 *   the same                  exit 0, nothing printed
 *   they differ               "A B differ: char N, line M" on stdout, exit 1
 *   one ends first            "cmp: EOF on 'A' after byte N, line M" when
 *                             the common part ends a line, "..., in line M"
 *                             inside one, "... which is empty" if empty;
 *                             on stderr, exit 1
 *   a file cannot be opened   "cmp: A: No such file or directory", exit 2
 * Bytes and lines count from 1, the line being the one the difference is on.
 */
#include "ulib.h"

static void put_num(int fd, unsigned v) {
    char d[12]; int k = 0; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) { char c = d[--k]; sys_write(fd, &c, 1); }
}
static void out(int fd, const char *s) { sys_write(fd, s, strlen_(s)); }

int main(void) {
    char *argv[4];
    int argc = uargv(argv, 4);
    if (argc != 2) { out(FD_STDERR, "usage: cmp.elf FILE1 FILE2\n"); return 2; }
    int fa = sys_open(argv[0], O_READ);
    if (fa < 0) { out(FD_STDERR, "cmp: "); out(FD_STDERR, argv[0]); out(FD_STDERR, ": No such file or directory\n"); return 2; }
    int fb = sys_open(argv[1], O_READ);
    if (fb < 0) { out(FD_STDERR, "cmp: "); out(FD_STDERR, argv[1]); out(FD_STDERR, ": No such file or directory\n"); sys_close(fa); return 2; }
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
            out(FD_STDERR, "cmp: EOF on '"); out(FD_STDERR, who); out(FD_STDERR, "'");
            if (byte == 0) out(FD_STDERR, " which is empty\n");
            else {
                out(FD_STDERR, " after byte "); put_num(FD_STDERR, byte);
                if (last_nl) { out(FD_STDERR, ", line "); put_num(FD_STDERR, lines); }
                else         { out(FD_STDERR, ", in line "); put_num(FD_STDERR, lines + 1); }
                out(FD_STDERR, "\n");
            }
            rc = 1; break;
        }
        char x = ba[ia++], y = bb[ib++];
        if (x != y) {
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

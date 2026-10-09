/* carve.c - a file in pieces, split under a kitchen name (v0.60.123).
 *
 *   cook carve.elf FILE                xaa, xab, ...: 1000 lines each
 *   cook carve.elf -l 10 FILE part.    part.aa, part.ab, ...: 10 lines each
 *   cook carve.elf -b 1K FILE          1024 bytes each (K, M as 1024s)
 *
 * As GNU's split: FILE or - (or none) is stdin; PREFIX x when not given;
 * suffixes aa to yz (650 pieces; GNU's go on, longer, past that). A piece
 * is made when its first byte comes, so nothing in makes nothing. A last
 * line without a newline stays without one.
 */
#include "ulib.h"

static char name[96];
static int plen, piece = -1, out = -1;

static int next_piece(void) {
    if (out >= 0) sys_close(out);
    piece++;
    if (piece >= 650) { eprint("carve.elf: more than 650 pieces\n"); return -1; }
    name[plen] = (char)('a' + piece / 26);
    name[plen + 1] = (char)('a' + piece % 26);
    name[plen + 2] = '\0';
    out = sys_open(name, O_WRITE);
    if (out < 0) { eprint("carve.elf: cannot write "); eprint(name); eprint("\n"); return -1; }
    return 0;
}

static int size_of(const char *s, unsigned *v) {
    unsigned n = 0; int any = 0;
    for (; *s >= '0' && *s <= '9'; s++) { n = n * 10 + (unsigned)(*s - '0'); any = 1; }
    if (*s == 'K' || *s == 'k') { n *= 1024; s++; }
    else if (*s == 'M' || *s == 'm') { n *= 1024 * 1024; s++; }
    if (!any || *s || !n) return -1;
    *v = n; return 0;
}

int main(void) {
    char *tok[8];
    int nt = uargv(tok, 8), a = 0, bytes = 0;
    unsigned size = 1000;
    for (; a < nt && tok[a][0] == '-' && tok[a][1]; a++) {
        if ((tok[a][1] == 'l' || tok[a][1] == 'b') && !tok[a][2] && a + 1 < nt) {
            bytes = tok[a][1] == 'b';
            if (size_of(tok[++a], &size) < 0) { eprint("carve.elf: invalid size: "); eprint(tok[a]); eprint("\n"); return 1; }
        } else { eprint("usage: carve.elf [-l N | -b N] [FILE [PREFIX]]\n"); return 1; }
    }
    int fd = FD_STDIN;
    if (a < nt && !(tok[a][0] == '-' && !tok[a][1])) {
        fd = sys_open(tok[a], O_READ);
        if (fd < 0) { eprint("carve.elf: "); eprint(tok[a]); eprint(": cannot open\n"); return 1; }
    }
    const char *prefix = a + 1 < nt ? tok[a + 1] : "x";
    for (plen = 0; prefix[plen] && plen < 90; plen++) name[plen] = prefix[plen];
    static char buf[1024];
    unsigned in_piece = size;                       /* full: the first byte opens a piece */
    int got;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0) {
        int i = 0;
        while (i < got) {
            if (in_piece >= size) { if (next_piece() < 0) return 1; in_piece = 0; }
            int start = i;
            if (bytes) {
                unsigned room = size - in_piece;
                int take = (unsigned)(got - i) < room ? got - i : (int)room;
                i += take; in_piece += (unsigned)take;
            } else {
                while (i < got && in_piece < size) { if (buf[i++] == '\n') in_piece++; }
            }
            sys_write(out, buf + start, i - start);
        }
    }
    if (out >= 0) sys_close(out);
    if (fd != FD_STDIN) sys_close(fd);
    return 0;
}

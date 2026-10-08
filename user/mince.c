/* mince.c - a file in hex (v0.60.14).
 *
 *   cook mince.elf RECIPE.TXT
 *   spoon.elf RECIPE.TXT | mince.elf
 *
 * In `hexdump -C` form: an eight-digit offset, sixteen bytes in two groups
 * of eight, the printable ones between bars; a full line the same as the
 * one before it is shown once as `*`, and the last line is the length.
 * Empty input prints nothing. It streams, so any size works.
 */
#include "ulib.h"

static char ob[4096];
static int  on;

static void put(char c) {
    if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
    ob[on++] = c;
}
static void put_hex(unsigned v, int digits) {
    static const char hx[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; i--) put(hx[(v >> (4 * i)) & 15]);
}

static void line(unsigned off, const unsigned char *b, int n) {
    put_hex(off, 8); put(' '); put(' ');
    for (int i = 0; i < 16; i++) {
        if (i < n) { put_hex(b[i], 2); put(' '); }
        else       { put(' '); put(' '); put(' '); }
        if (i == 7) put(' ');
    }
    put(' '); put('|');
    for (int i = 0; i < n; i++) put(b[i] >= 0x20 && b[i] < 0x7f ? (char)b[i] : '.');
    put('|'); put('\n');
}

int main(void) {
    char *tok[4];
    int nt = uargv(tok, 4);
    if (nt > 1) { print("usage: mince.elf [file]\n"); return 2; }
    int fd = FD_STDIN;
    if (nt == 1) {
        fd = sys_open(tok[0], O_READ);
        if (fd < 0) { print("mince: cannot open "); print(tok[0]); print("\n"); return 1; }
    }
    unsigned char cur[16], prev[16];
    int have = 0, had_prev = 0, starred = 0, got;
    unsigned off = 0;
    char in[4096];
    while ((got = sys_read(fd, in, sizeof(in))) > 0) {
        for (int i = 0; i < got; i++) {
            cur[have++] = (unsigned char)in[i];
            if (have < 16) continue;
            int same = had_prev;
            for (int k = 0; same && k < 16; k++) if (cur[k] != prev[k]) same = 0;
            if (same) { if (!starred) { put('*'); put('\n'); starred = 1; } }
            else { line(off, cur, 16); starred = 0; }
            for (int k = 0; k < 16; k++) prev[k] = cur[k];
            had_prev = 1; off += 16; have = 0;
        }
    }
    if (fd != FD_STDIN) sys_close(fd);
    if (have) { line(off, cur, have); off += (unsigned)have; }
    if (off) { put_hex(off, 8); put('\n'); }
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

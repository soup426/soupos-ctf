/* label.c - every line numbered (v0.60.9).
 *
 *   cook label.elf RECIPE.TXT
 *   spoon.elf RECIPE.TXT | label.elf
 *
 * As `nl -ba` numbers them: every line, empty ones included, gets its
 * number right-aligned in six columns and a tab; a number wider than six
 * simply runs wider. A last line with no newline gets one, as nl gives it.
 * nl's logical-page delimiters (lines of \: pairs) are not special here.
 * The input streams through in 4 KB pieces, so any size works.
 */
#include "ulib.h"

static char ob[4096];
static int  on;

static void put(char c) {
    if (on == (int)sizeof(ob)) { sys_write(FD_STDOUT, ob, on); on = 0; }
    ob[on++] = c;
}

static void put_number(unsigned n) {
    char d[12]; int k = 0;
    do { d[k++] = (char)('0' + n % 10); n /= 10; } while (n);
    for (int pad = k; pad < 6; pad++) put(' ');
    while (k) put(d[--k]);
    put('\t');
}

int main(void) {
    char *tok[4];
    int nt = uargv(tok, 4);
    if (nt > 1) { print("usage: label.elf [file]\n"); return 2; }
    int fd = FD_STDIN;
    if (nt == 1) {
        fd = sys_open(tok[0], O_READ);
        if (fd < 0) { print("label: cannot open "); print(tok[0]); print("\n"); return 1; }
    }
    char in[4096];
    unsigned line = 0;
    int at_start = 1, got;
    while ((got = sys_read(fd, in, sizeof(in))) > 0) {
        for (int i = 0; i < got; i++) {
            if (at_start) { put_number(++line); at_start = 0; }
            put(in[i]);
            if (in[i] == '\n') at_start = 1;
        }
    }
    if (!at_start) put('\n');
    if (fd != FD_STDIN) sys_close(fd);
    if (on) sys_write(FD_STDOUT, ob, on);
    return 0;
}

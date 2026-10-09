/* flip.c - every line backwards (v0.60.8).
 *
 *   cook flip.elf RECIPE.TXT
 *   spoon.elf RECIPE.TXT | flip.elf
 *
 * Byte by byte, as rev does under LC_ALL=C. The newline stays at the end of
 * its line, and a last line with no newline comes out with none. The whole
 * input is read first, then flipped into a second buffer and written once.
 */
#include "ulib.h"

int main(void) {
    char *tok[4];
    int nt = uargv(tok, 4);
    if (nt > 1) { print("usage: flip.elf [file]\n"); return 2; }
    int fd = FD_STDIN;
    if (nt == 1) {
        fd = sys_open(tok[0], O_READ);
        if (fd < 0) { print("flip: cannot open "); print(tok[0]); print("\n"); return 1; }
    }
    /* Read it all: grow by doubling (the bump allocator keeps the old). */
    int cap = 4096, len = 0, got;
    char *all = malloc((unsigned)cap);
    for (;;) {
        if (len == cap) {
            char *bigger = malloc((unsigned)(cap * 2));
            for (int i = 0; i < len; i++) bigger[i] = all[i];
            all = bigger; cap *= 2;
        }
        got = sys_read(fd, all + len, cap - len);
        if (got <= 0) break;
        len += got;
    }
    if (fd != FD_STDIN) sys_close(fd);
    if (len == 0) return 0;
    char *out = malloc((unsigned)len);
    int start = 0;
    while (start < len) {
        int end = start;
        while (end < len && all[end] != '\n') end++;
        for (int i = 0; i < end - start; i++) out[start + i] = all[end - 1 - i];
        if (end < len) out[end] = '\n';
        start = end + 1;
    }
    sys_write(FD_STDOUT, out, len);
    return 0;
}

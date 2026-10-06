/* unhex.c - hex text to binary, so a player can materialise a crafted file.
 *
 *   cook unhex.elf IN.HEX OUT.ELF
 *
 * soupOS has no upload, and a soupyc script cannot emit the NUL bytes an ELF
 * header needs: its strings are a NUL-terminated char[48], so chr(0) is not
 * expressible. sys_write takes an explicit length, so this can write any byte.
 * Whitespace in the input is ignored, so the hex may be laid out over many
 * lines with `jot`.
 */
#include "ulib.h"

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main(void) {
    char args[128];
    sys_args(args, sizeof(args));

    /* split "IN.HEX OUT.ELF" */
    char *in = args, *out = 0;
    int i = 0;
    while (args[i] && args[i] != ' ') i++;
    if (args[i] == ' ') { args[i] = '\0'; out = &args[i + 1]; }
    if (!out || !out[0]) {
        print("usage: cook unhex.elf IN.HEX OUT.ELF\n");
        return 1;
    }

    int fi = sys_open(in, 0);
    if (fi < 0) { print("unhex: cannot open input\n"); return 1; }

    static char  text[16384];
    static char  bin[8192];
    int n = 0, r;
    while ((r = sys_read(fi, text + n, (int)sizeof(text) - n)) > 0) {
        n += r;
        if (n >= (int)sizeof(text)) break;
    }
    sys_close(fi);

    int nb = 0, hi = -1;
    for (int k = 0; k < n; k++) {
        int v = hexval(text[k]);
        if (v < 0) continue;                 /* skip spaces and newlines */
        if (hi < 0) { hi = v; continue; }
        if (nb < (int)sizeof(bin)) bin[nb++] = (char)((hi << 4) | v);
        hi = -1;
    }

    int fo = sys_open(out, 1);
    if (fo < 0) { print("unhex: cannot open output\n"); return 1; }
    int w = sys_write(fo, bin, nb);
    sys_close(fo);

    print("unhex: wrote ");
    print_int(w);
    print(" bytes\n");
    return 0;
}

/* tr.c - translate, delete or squeeze characters (v0.56.9).
 *
 *   cook cat.elf X | tr.elf a-z A-Z          upper case
 *   cook cat.elf X | tr.elf -d 0-9           digits gone
 *   cook cat.elf X | tr.elf -s a-z           runs of a letter squeezed to one
 *   cook cat.elf X | tr.elf -s abc xyz       translate, then squeeze xyz
 *
 * GNU tr's rules in the C locale: sets take ranges (a-z) and the escapes
 * \n \t \; a '-' at either end of a set is itself; when translating, the
 * second set is padded with its last character to the first's length, and
 * a character named twice in the first set takes its later mapping. -d
 * deletes the first set; -s squeezes runs of the last set named; -d -s
 * deletes the first and squeezes the second. stdin to stdout, bytes.
 */
#include "ulib.h"

static int expand(const char *s, unsigned char *out) {
    int n = 0;
    for (int i = 0; s[i] && n < 512; ) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' && s[i + 1]) {
            char e = s[i + 1];
            c = (unsigned char)(e == 'n' ? '\n' : e == 't' ? '\t' : e);
            i += 2;
        } else i++;
        if (s[i] == '-' && s[i + 1] && n < 512) {        /* a range c-d */
            unsigned char d = (unsigned char)s[i + 1];
            int skip = 2;
            if (d == '\\' && s[i + 2]) { char e = s[i + 2]; d = (unsigned char)(e == 'n' ? '\n' : e == 't' ? '\t' : e); skip = 3; }
            if (d < c) return -1;                        /* tr refuses a reversed range */
            for (int k = c; k <= d && n < 512; k++) out[n++] = (unsigned char)k;
            i += skip;
            continue;
        }
        out[n++] = c;
    }
    return n;
}

int main(void) {
    char *tok[6];
    int nt = uargv(tok, 6);        /* sh's splitting, quotes kept together (v0.57.2) */
    int del = 0, squeeze = 0, t = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1] && (tok[t][1] == 'd' || tok[t][1] == 's'); t++)
        for (char *f = tok[t] + 1; *f; f++) {
            if (*f == 'd') del = 1;
            else if (*f == 's') squeeze = 1;
            else { print("usage: tr.elf [-d] [-s] SET1 [SET2]\n"); return 1; }
        }
    int nsets = nt - t;
    unsigned char s1[512], s2[512];
    int n1 = 0, n2 = 0;
    if (nsets < 1 || nsets > 2) { print("usage: tr.elf [-d] [-s] SET1 [SET2]\n"); return 1; }
    n1 = expand(tok[t], s1);
    if (nsets == 2) n2 = expand(tok[t + 1], s2);
    if (n1 < 0 || n2 < 0) { print("tr: a range runs backwards\n"); return 1; }
    int translate = !del && nsets == 2;
    if (!del && !squeeze && nsets != 2) { print("tr: two sets are needed to translate\n"); return 1; }
    if (translate && n2 == 0) { print("tr: the second set is empty\n"); return 1; }

    unsigned char map[256], in_del[256], in_sq[256];
    for (int i = 0; i < 256; i++) { map[i] = (unsigned char)i; in_del[i] = in_sq[i] = 0; }
    if (translate) for (int i = 0; i < n1; i++) map[s1[i]] = s2[i < n2 ? i : n2 - 1];
    if (del) for (int i = 0; i < n1; i++) in_del[s1[i]] = 1;
    if (squeeze) {
        const unsigned char *sq = (nsets == 2) ? s2 : s1;
        int nq = (nsets == 2) ? n2 : n1;
        for (int i = 0; i < nq; i++) in_sq[sq[i]] = 1;
    }
    char buf[512], out[512];
    int got, last = -1;
    while ((got = sys_read(FD_STDIN, buf, sizeof(buf))) > 0) {
        int o = 0;
        for (int i = 0; i < got; i++) {
            unsigned char c = (unsigned char)buf[i];
            if (del && in_del[c]) continue;
            c = map[c];
            if (squeeze && in_sq[c] && last == c) continue;
            out[o++] = (char)c;
            last = c;
        }
        sys_write(FD_STDOUT, out, o);
    }
    return 0;
}

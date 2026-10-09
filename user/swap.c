/* swap.c - translate, delete or squeeze characters (v0.56.9).
 *
 *   cook spoon.elf X | swap.elf a-z A-Z          upper case
 *   cook spoon.elf X | swap.elf -d 0-9           digits gone
 *   cook spoon.elf X | swap.elf -s a-z           runs of a letter squeezed to one
 *   cook spoon.elf X | swap.elf -s abc xyz       translate, then squeeze xyz
 *
 * GNU tr's rules in the C locale: sets take ranges (a-z) and the escapes
 * \n \t \; a '-' at either end of a set is itself; when translating, the
 * second set is padded with its last character to the first's length, and
 * a character named twice in the first set takes its later mapping. -d
 * deletes the first set; -s squeezes runs of the last set named; -d -s
 * deletes the first and squeezes the second. stdin to stdout, bytes.
 *
 * v0.60.132: [:alpha:] [:digit:] [:alnum:] [:upper:] [:lower:] [:space:]
 * [:punct:] [:blank:] [:xdigit:] [:cntrl:] [:print:] [:graph:] in a set,
 * each in byte order (so [:lower:] [:upper:] translates letter for
 * letter); \NNN octal and \\ \a \b \f \r \v beside \n \t; -c (or -C)
 * takes every byte not in the first set, in order, for -d, -s and to
 * translate, as GNU tr's.
 */
#include "ulib.h"

static int esc(const char *s, int *i) {            /* s[*i] is the \ */
    char e = s[*i + 1];
    *i += 2;
    if (e >= '0' && e <= '7') {                     /* up to three octal digits */
        int v = e - '0', d = 1;
        while (d < 3 && s[*i] >= '0' && s[*i] <= '7') { v = v * 8 + (s[(*i)++] - '0'); d++; }
        return v & 0xff;
    }
    return e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'a' ? 7 : e == 'b' ? 8 :
           e == 'f' ? 12 : e == 'v' ? 11 : (unsigned char)e;
}
static int memcmp_(const char *a, const char *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 1; return 0; }
static int in_class(const char *nm, int nl, int c) {
    #define IS(x) (nl == (int)sizeof(x) - 1 && !memcmp_(nm, x, nl))
    int up = c >= 'A' && c <= 'Z', lo = c >= 'a' && c <= 'z', dg = c >= '0' && c <= '9';
    if (IS("alpha")) return up || lo;
    if (IS("digit")) return dg;
    if (IS("alnum")) return up || lo || dg;
    if (IS("upper")) return up;
    if (IS("lower")) return lo;
    if (IS("space")) return c == ' ' || (c >= 9 && c <= 13);
    if (IS("blank")) return c == ' ' || c == '\t';
    if (IS("punct")) return c > 32 && c < 127 && !up && !lo && !dg;
    if (IS("xdigit")) return dg || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (IS("cntrl")) return c < 32 || c == 127;
    if (IS("print")) return c >= 32 && c < 127;
    if (IS("graph")) return c > 32 && c < 127;
    #undef IS
    return -1;
}

/* -1 a backwards range, -2 an unknown class */
static int expand(const char *s, unsigned char *out) {
    int n = 0;
    for (int i = 0; s[i] && n < 512; ) {
        if (s[i] == '[' && s[i + 1] == ':') {           /* [:class:] */
            int j = i + 2;
            while (s[j] && !(s[j] == ':' && s[j + 1] == ']')) j++;
            if (s[j]) {
                if (in_class(s + i + 2, j - i - 2, 'a') < 0) return -2;
                for (int c = 0; c < 256 && n < 512; c++) if (in_class(s + i + 2, j - i - 2, c) > 0) out[n++] = (unsigned char)c;
                i = j + 2;
                continue;
            }
        }
        unsigned char c;
        if (s[i] == '\\' && s[i + 1]) c = (unsigned char)esc(s, &i);
        else c = (unsigned char)s[i++];
        if (s[i] == '-' && s[i + 1] && n < 512) {        /* a range c-d */
            int k = i + 1;
            unsigned char d;
            if (s[k] == '\\' && s[k + 1]) d = (unsigned char)esc(s, &k); else d = (unsigned char)s[k++];
            if (d < c) return -1;                        /* tr refuses a reversed range */
            for (int x = c; x <= d && n < 512; x++) out[n++] = (unsigned char)x;
            i = k;
            continue;
        }
        out[n++] = c;
    }
    return n;
}

int main(void) {
    char *tok[6];
    int nt = uargv(tok, 6);        /* sh's splitting, quotes kept together (v0.57.2) */
    int del = 0, squeeze = 0, comp = 0, t = 0;
    for (; t < nt && tok[t][0] == '-' && tok[t][1] && (tok[t][1] == 'd' || tok[t][1] == 's' || tok[t][1] == 'c' || tok[t][1] == 'C'); t++)
        for (char *f = tok[t] + 1; *f; f++) {
            if (*f == 'd') del = 1;
            else if (*f == 's') squeeze = 1;
            else if (*f == 'c' || *f == 'C') comp = 1;
            else { print("usage: swap.elf [-cds] SET1 [SET2]\n"); return 1; }
        }
    int nsets = nt - t;
    unsigned char s1[512], s2[512];
    int n1 = 0, n2 = 0;
    if (nsets < 1 || nsets > 2) { print("usage: swap.elf [-d] [-s] SET1 [SET2]\n"); return 1; }
    n1 = expand(tok[t], s1);
    if (nsets == 2) n2 = expand(tok[t + 1], s2);
    if (n1 == -2 || n2 == -2) { print("swap: an unknown [:class:]\n"); return 1; }
    if (n1 < 0 || n2 < 0) { print("swap: a range runs backwards\n"); return 1; }
    if (comp) {                                     /* every byte not in SET1, in order */
        unsigned char has[256];
        for (int i = 0; i < 256; i++) has[i] = 0;
        for (int i = 0; i < n1; i++) has[s1[i]] = 1;
        n1 = 0;
        for (int i = 0; i < 256; i++) if (!has[i]) s1[n1++] = (unsigned char)i;
    }
    int translate = !del && nsets == 2;
    if (!del && !squeeze && nsets != 2) { print("swap: two sets are needed to translate\n"); return 1; }
    if (translate && n2 == 0) { print("swap: the second set is empty\n"); return 1; }

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

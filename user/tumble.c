/* tumble.c - lines in a random order, shuf under a kitchen name
 * (v0.60.118): tossed like a salad.
 *
 *   cook tumble.elf FILE           FILE's lines (or stdin's), shuffled
 *   cook tumble.elf -n 3 FILE      three of them
 *   cook tumble.elf -i 1-10        the numbers 1 to 10, shuffled
 *
 * As GNU's shuf. Every order equally likely: Fisher-Yates, each pick from
 * the kernel's entropy pool (SYS_RANDOM) with the uneven top of the range
 * thrown back. A last line with no newline gets one.
 */
#include "ulib.h"

static unsigned rnd_buf[64];
static int rnd_left;
static unsigned rnd32(void) {
    if (!rnd_left) { sys_random(rnd_buf, sizeof(rnd_buf)); rnd_left = 64; }
    return rnd_buf[--rnd_left];
}
static unsigned below(unsigned n) {                  /* 0..n-1, every one as likely */
    unsigned lim = 0xFFFFFFFFu - (0xFFFFFFFFu % n + 1) % n;
    for (;;) { unsigned r = rnd32(); if (r <= lim) return r % n; }
}
static int num(const char *s, unsigned *out) {
    unsigned v = 0; int any = 0;
    for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (unsigned)(*s - '0'); any = 1; }
    if (!any || *s) return -1;
    *out = v; return 0;
}

int main(void) {
    char *tok[8];
    int nt = uargv(tok, 8), a = 0;
    unsigned count = 0xFFFFFFFFu, lo = 0, hi = 0;
    int range = 0;
    for (; a < nt && tok[a][0] == '-' && tok[a][1]; a++) {
        if (tok[a][1] == 'n' && !tok[a][2] && a + 1 < nt) {
            if (num(tok[++a], &count) < 0) { eprint("tumble.elf: invalid line count: "); eprint(tok[a]); eprint("\n"); return 1; }
        } else if (tok[a][1] == 'i' && !tok[a][2] && a + 1 < nt) {
            char *r = tok[++a], *dash = r;
            while (*dash && *dash != '-') dash++;
            if (!*dash) { eprint("tumble.elf: invalid input range: "); eprint(r); eprint("\n"); return 1; }
            *dash = '\0';
            if (num(r, &lo) < 0 || num(dash + 1, &hi) < 0 || lo > hi + 1) {
                *dash = '-'; eprint("tumble.elf: invalid input range: "); eprint(r); eprint("\n"); return 1;
            }
            range = 1;
        } else { eprint("usage: tumble.elf [-n N] [-i LO-HI | FILE]\n"); return 1; }
    }
    if (range) {
        unsigned n = hi - lo + 1;
        if (n > 65536) { eprint("tumble.elf: a range of 65536 at most\n"); return 1; }
        unsigned *v = malloc(n * sizeof(unsigned));
        if (!v && n) { eprint("tumble.elf: out of memory\n"); return 1; }
        for (unsigned i = 0; i < n; i++) v[i] = lo + i;
        unsigned out = count < n ? count : n;
        for (unsigned i = 0; i < out; i++) {        /* the first out places, each from what is left */
            unsigned j = i + below(n - i);
            unsigned t = v[i]; v[i] = v[j]; v[j] = t;
            print_int((int)v[i]); print("\n");
        }
        return 0;
    }
    int fd = FD_STDIN;
    if (a < nt && !(tok[a][0] == '-' && !tok[a][1])) {
        fd = sys_open(tok[a], O_READ);
        if (fd < 0) { eprint("tumble.elf: "); eprint(tok[a]); eprint(": cannot open\n"); return 1; }
    }
    /* the whole input, then where each line starts */
    int cap = 4096, len = 0, got;
    char *text = malloc((unsigned)cap);
    for (;;) {
        if (len == cap) {
            char *bigger = malloc((unsigned)cap * 2);
            if (!bigger) { eprint("tumble.elf: out of memory\n"); return 1; }
            for (int i = 0; i < len; i++) bigger[i] = text[i];
            text = bigger; cap *= 2;                 /* the bump allocator keeps the old: fine for one run */
        }
        got = sys_read(fd, text + len, cap - len);
        if (got <= 0) break;
        len += got;
    }
    if (len && text[len - 1] != '\n') {
        if (len == cap) { char *b2 = malloc((unsigned)cap + 1); for (int i = 0; i < len; i++) b2[i] = text[i]; text = b2; }
        text[len++] = '\n';
    }
    unsigned n = 0;
    for (int i = 0; i < len; i++) if (text[i] == '\n') n++;
    unsigned *at = malloc((n + 1) * sizeof(unsigned));
    unsigned k = 0; at[0] = 0;
    for (int i = 0; i < len; i++) if (text[i] == '\n') at[++k] = (unsigned)i + 1;
    unsigned *ord = malloc((n + 1) * sizeof(unsigned));
    for (unsigned i = 0; i < n; i++) ord[i] = i;
    unsigned out = count < n ? count : n;
    for (unsigned i = 0; i < out; i++) {
        unsigned j = i + below(n - i);
        unsigned t = ord[i]; ord[i] = ord[j]; ord[j] = t;
        sys_write(FD_STDOUT, text + at[ord[i]], (int)(at[ord[i] + 1] - at[ord[i]]));
    }
    return 0;
}

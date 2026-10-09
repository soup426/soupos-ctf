/* potluck.c - what two sorted files brought, comm under a kitchen name
 * (v0.60.111).
 *
 *   cook potluck.elf A B        only in A | only in B (a tab) | both (two)
 *   cook potluck.elf -12 A B    only the lines both brought
 *
 * As GNU's comm, in byte order: -1 -2 -3 leave out a column (and combine,
 * -12), each column after the first indented by a tab for every earlier
 * one still shown; a line repeated is matched once for each time; - is
 * stdin. A last line with no newline gets one.
 */
#include "ulib.h"

typedef struct { int fd, len, pos, eof; char buf[512]; } rd_t;
static char la[1024], lb[1024];

/* the next line into out, no newline; 0 at the end */
static int getline_(rd_t *r, char *out, int max) {
    int n = 0, any = 0;
    for (;;) {
        if (r->pos >= r->len) {
            if (r->eof) break;
            r->len = sys_read(r->fd, r->buf, sizeof(r->buf)); r->pos = 0;
            if (r->len <= 0) { r->len = 0; r->eof = 1; break; }
        }
        char c = r->buf[r->pos++];
        any = 1;
        if (c == '\n') break;
        if (n < max - 1) out[n++] = c;
    }
    out[n] = '\0';
    return any;
}
static int cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static int show[4] = { 0, 1, 1, 1 };
static void put(int col, const char *s) {
    if (!show[col]) return;
    for (int c = 1; c < col; c++) if (show[c]) sys_write(FD_STDOUT, "\t", 1);
    print(s); print("\n");
}
static int open_(const char *name) {
    if (name[0] == '-' && !name[1]) return FD_STDIN;
    return sys_open(name, O_READ);
}

int main(void) {
    char *tok[6];
    int nt = uargv(tok, 6), a = 0;
    for (; a < nt && tok[a][0] == '-' && tok[a][1]; a++)
        for (const char *c = tok[a] + 1; *c; c++) {
            if (*c >= '1' && *c <= '3') show[*c - '0'] = 0;
            else { eprint("potluck.elf: unknown option -"); char o[2] = { *c, 0 }; eprint(o); eprint("\n"); return 1; }
        }
    if (nt - a != 2) { eprint("usage: potluck.elf [-123] FILE1 FILE2\n"); return 1; }
    static rd_t A, B;
    A.fd = open_(tok[a]); B.fd = open_(tok[a + 1]);
    if (A.fd < 0) { eprint("potluck.elf: cannot open "); eprint(tok[a]); eprint("\n"); return 1; }
    if (B.fd < 0) { eprint("potluck.elf: cannot open "); eprint(tok[a + 1]); eprint("\n"); return 1; }
    int ha = getline_(&A, la, sizeof(la)), hb = getline_(&B, lb, sizeof(lb));
    while (ha || hb) {
        int d = !ha ? 1 : !hb ? -1 : cmp(la, lb);
        if (d < 0) { put(1, la); ha = getline_(&A, la, sizeof(la)); }
        else if (d > 0) { put(2, lb); hb = getline_(&B, lb, sizeof(lb)); }
        else { put(3, la); ha = getline_(&A, la, sizeof(la)); hb = getline_(&B, lb, sizeof(lb)); }
    }
    return 0;
}

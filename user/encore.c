/* encore.c - the same line again and again, yes under a kitchen name
 * (v0.60.108): the kitchen calling for more.
 *
 *   cook encore.elf | cook skim.elf -n 3      y, three times
 *   cook encore.elf more soup                 "more soup" until stopped
 *
 * Its words joined by spaces, or y; until a write fails, which is what
 * happens when whatever reads it has gone (or Ctrl-C, or kill).
 */
#include "ulib.h"

int main(void) {
    char *tok[16];
    int nt = uargv(tok, 16);
    static char line[256];
    int n = 0;
    if (!nt) line[n++] = 'y';
    for (int a = 0; a < nt; a++) {
        if (a && n < 254) line[n++] = ' ';
        for (const char *c = tok[a]; *c && n < 254; c++) line[n++] = *c;
    }
    line[n++] = '\n';
    static char buf[1024];                         /* lines enough for a write that counts */
    int b = 0;
    while (b + n <= (int)sizeof(buf)) { for (int k = 0; k < n; k++) buf[b++] = line[k]; }
    for (;;) if (sys_write(FD_STDOUT, buf, b) <= 0) return 0;
}

/* marinate.c - sleep under a kitchen name (v0.60.101).
 *
 *   cook marinate.elf 2       two seconds
 *   cook marinate.elf 0.5     half of one (tenths are the grain)
 *
 * Sleeps in steps of a tenth of a second (SYS_SLEEP), so a kill or a
 * Ctrl-C that lands meanwhile ends it at the next step rather than after
 * the whole wait. Status 2 for a word that is not a number of seconds.
 */
#include "ulib.h"

int main(void) {
    char *tok[2];
    int nt = uargv(tok, 2);
    if (nt != 1) { print("usage: marinate.elf SECONDS\n"); return 2; }
    const char *c = tok[0];
    unsigned whole = 0, tenth = 0; int any = 0;
    while (*c >= '0' && *c <= '9') { whole = whole * 10 + (unsigned)(*c++ - '0'); any = 1; }
    if (*c == '.' && c[1] >= '0' && c[1] <= '9') { tenth = (unsigned)(c[1] - '0'); c += 2; any = 1; while (*c >= '0' && *c <= '9') c++; }
    if (!any || *c) { print("marinate.elf: not a number of seconds\n"); return 2; }
    unsigned steps = whole * 10 + tenth;
    for (unsigned k = 0; k < steps; k++) sys_sleep(100);
    return 0;
}

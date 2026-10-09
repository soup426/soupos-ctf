/* stalk.c - what leads to a name's last part, dirname under a kitchen
 * name (v0.60.105).
 *
 *   cook stalk.elf /home/chef/soup.txt       /home/chef
 *   cook stalk.elf soup.txt                  .
 *
 * As GNU's dirname, one line for each NAME: trailing slashes do not count,
 * a name with no / is in ., and what is left after the last part is
 * trimmed of its own trailing slashes, down to / at the least.
 */
#include "ulib.h"

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

int main(void) {
    char *tok[16];
    int nt = uargv(tok, 16);
    if (nt < 1) { print("usage: stalk.elf NAME...\n"); return 2; }
    for (int a = 0; a < nt; a++) {
        char *s = tok[a];
        int n = slen(s);
        while (n > 1 && s[n - 1] == '/') n--;          /* trailing slashes */
        while (n > 0 && s[n - 1] != '/') n--;          /* the last part */
        if (n == 0) { print(".\n"); continue; }
        while (n > 1 && s[n - 1] == '/') n--;          /* the slashes before it */
        s[n] = '\0';
        print(s); print("\n");
    }
    return 0;
}

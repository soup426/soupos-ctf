/* peel.c - a name's last part, basename under a kitchen name (v0.60.105).
 *
 *   cook peel.elf /home/chef/soup.txt        soup.txt
 *   cook peel.elf /home/chef/soup.txt .txt   soup
 *
 * As GNU's basename: trailing slashes do not count, a name of slashes
 * only is /, and SUFFIX comes off the end unless it is all there is.
 */
#include "ulib.h"

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

int main(void) {
    char *tok[3];
    int nt = uargv(tok, 3);
    if (nt < 1 || nt > 2) { print("usage: peel.elf NAME [SUFFIX]\n"); return 2; }
    char *s = tok[0];
    int n = slen(s);
    while (n > 1 && s[n - 1] == '/') n--;              /* trailing slashes */
    if (n == 1 && s[0] == '/') { print("/\n"); return 0; }
    int st = n;
    while (st > 0 && s[st - 1] != '/') st--;
    s += st; n -= st;
    s[n] = '\0';
    if (nt == 2) {
        int sl = slen(tok[1]);
        if (sl > 0 && sl < n) {
            int same = 1;
            for (int k = 0; k < sl; k++) if (s[n - sl + k] != tok[1][k]) { same = 0; break; }
            if (same) s[n - sl] = '\0';
        }
    }
    print(s); print("\n");
    return 0;
}

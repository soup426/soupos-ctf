/* labels.c - what the shell handed this program (v0.60.26).
 *
 *   cook labels.elf          every handed variable, NAME=value, one a line
 *   cook labels.elf NAME     just its value; status 1 if it was not handed
 *
 * env and printenv under a kitchen name: the pantry's labels. The shell
 * hands a variable with `hand NAME`; the kernel keeps them with the process
 * (SYS_ENV) and ulib's getenv reads them.
 */
#include "ulib.h"

int main(void) {
    char *tok[4];
    int nt = uargv(tok, 4);
    if (nt > 1) { print("usage: labels.elf [NAME]\n"); return 2; }
    if (nt == 1) {
        const char *v = getenv(tok[0]);
        if (!v) return 1;
        print(v); print("\n");
        return 0;
    }
    static char env[512];
    int n = sys_env(env, sizeof(env));
    if (n > 0) sys_write(FD_STDOUT, env, n);
    return 0;
}

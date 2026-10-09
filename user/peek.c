/* peek.c - list a directory from ring 3, through SYS_READDIR (v0.44.0).
 *
 *   cook peek.elf             the cwd's root (/)
 *   cook peek.elf /etc        any directory
 *   cook peek.elf -l /etc     with mode, owner and size, through SYS_STAT
 *                           (v0.59.1), in the columns `serve` uses
 *
 * One line per entry on stdout, "name  size" or "name/" for a directory, and
 * the same as machine-readable "LS ..." lines on stderr, which the console
 * sends to the serial log, so a test can compare them with mtools' listing
 * of the same image. With -l the stderr lines are "LSL mode owner size name".
 */
#include "ulib.h"

static void put_mode(unsigned m, char *s) {
    s[0] = (m & 0x20) ? 'r' : '-'; s[1] = (m & 0x10) ? 'w' : '-'; s[2] = (m & 0x08) ? 'x' : '-';
    s[3] = (m & 0x04) ? 'r' : '-'; s[4] = (m & 0x02) ? 'w' : '-'; s[5] = (m & 0x01) ? 'x' : '-';
    s[6] = 0;
}
static void pad(const char *s, int w) { print(s); for (int n = strlen_(s); n < w; n++) print(" "); }

int main(void) {
    char *argv[4];
    int argc = uargv(argv, 4), longf = 0, a = 0;
    if (a < argc && argv[a][0] == '-' && argv[a][1] == 'l' && !argv[a][2]) { longf = 1; a++; }
    const char *path = a < argc ? argv[a] : "/";

    udirent_t e;
    int count = 0, r;
    while ((r = sys_readdir(path, count, &e)) == 1) {
        if (longf) {
            char full[160]; int k = 0;
            for (const char *p = path; *p && k < 150; p++) full[k++] = *p;
            if (k == 0 || full[k - 1] != '/') full[k++] = '/';
            for (const char *p = e.name; *p && k < 159; p++) full[k++] = *p;
            full[k] = 0;
            ustat_t st; char ms[7] = "??????";
            const char *owner = "?";
            if (sys_stat(full, &st) == 0) { put_mode(st.mode, ms); owner = st.owner_name; }
            print(ms); print("  "); pad(owner, 10);
            print(e.name); if (e.is_dir) print("/"); else { print("  "); print_int((int)e.size); }
            print("\n");
            eprint("LSL "); eprint(ms); eprint(" "); eprint(owner); eprint(" ");
            eprint_int(e.is_dir ? -1 : (int)e.size); eprint(" "); eprint(e.name); eprint("\n");
        } else {
            print(e.name);
            if (e.is_dir) print("/");
            else { print("  "); print_int((int)e.size); }
            print("\n");
            eprint("LS "); eprint(e.is_dir ? "d " : "f "); eprint_int((int)e.size); eprint(" ");
            eprint(e.name); eprint("\n");
        }
        count++;
    }
    if (r < 0) { eprint("LS error: no such directory\n"); return 1; }
    eprint("LS end "); eprint_int(count); eprint(" entries\n");
    return 0;
}

/* ls.c - list a directory from ring 3, through SYS_READDIR (v0.44.0).
 *
 *   cook ls.elf          the root
 *   cook ls.elf /etc     any directory
 *
 * One line per entry on stdout, "name  size" or "name/" for a directory, and
 * the same as machine-readable "LS ..." lines on stderr, which the console
 * sends to the serial log, so a test can compare them with mtools' listing
 * of the same image.
 */
#include "ulib.h"

int main(void) {
    char path[128];
    int n = sys_args(path, sizeof(path));
    if (n <= 0) { path[0] = '/'; path[1] = 0; }

    udirent_t e;
    int count = 0, r;
    while ((r = sys_readdir(path, count, &e)) == 1) {
        print(e.name);
        if (e.is_dir) print("/");
        else { print("  "); print_int((int)e.size); }
        print("\n");
        eprint("LS "); eprint(e.is_dir ? "d " : "f "); eprint_int((int)e.size); eprint(" ");
        eprint(e.name); eprint("\n");
        count++;
    }
    if (r < 0) { eprint("LS error: no such directory\n"); return 1; }
    eprint("LS end "); eprint_int(count); eprint(" entries\n");
    return 0;
}

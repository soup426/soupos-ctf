/* spoon.c - copy a file, or stdin, to stdout.
 *
 *   cook spoon.elf README.TXT      copy a file
 *   cook spoon.elf < README.TXT    same thing through the shell's redirection
 *   cook call.elf hi | spoon.elf   a filter, because with no argument it reads
 *                                stdin like every other cat
 */
#include "ulib.h"

static void copy_fd(int fd) {
    char buf[256];
    int got;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0)
        sys_write(FD_STDOUT, buf, got);
}

int main(void) {
    char *argv[4];
    int n = uargv(argv, 4);       /* a quoted name may hold spaces (v0.57.2) */
    const char *path = n > 0 ? argv[0] : "";

    if (n <= 0) {                 /* no argument: be a filter */
        copy_fd(FD_STDIN);
        return 0;
    }

    int fd = sys_open(path, O_READ);
    if (fd < 0) { print("spoon: cannot open "); print(path); print("\n"); return 1; }
    copy_fd(fd);
    sys_close(fd);
    return 0;
}

/* cat.c - copy a file, or stdin, to stdout.
 *
 *   cook cat.elf README.TXT      copy a file
 *   cook cat.elf < README.TXT    same thing through the shell's redirection
 *   cook echo.elf hi | cat.elf   a filter, because with no argument it reads
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
    char path[96];
    int n = sys_args(path, sizeof(path));

    if (n <= 0) {                 /* no argument: be a filter */
        copy_fd(FD_STDIN);
        return 0;
    }

    int fd = sys_open(path, O_READ);
    if (fd < 0) { print("cat: cannot open "); print(path); print("\n"); return 1; }
    copy_fd(fd);
    sys_close(fd);
    return 0;
}

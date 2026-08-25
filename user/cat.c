/* cat.c - print a file's contents. Usage in soupOS: cook cat.elf <file> */
#include "ulib.h"

int main(void) {
    char path[96];
    int n = sys_args(path, sizeof(path));
    if (n <= 0) { print("usage: cook cat.elf <file>\n"); return 1; }

    int fd = sys_open(path, O_READ);
    if (fd < 0) { print("cat: cannot open "); print(path); print("\n"); return 1; }

    char buf[256];
    int got;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0)
        sys_write(FD_STDOUT, buf, got);

    sys_close(fd);
    return 0;
}

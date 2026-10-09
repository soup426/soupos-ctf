/* prod.c - prods the kernel with what a careful program would never
 * hand it, for the tests (v0.60.149): kitchen name, a poke with a fork.
 *
 *   cook prod.elf write     SYS_WRITE from above the heap's break
 *   cook prod.elf gap       SYS_WRITE from between the image and the heap
 *   cook prod.elf read      SYS_READ (a file) into above the break
 *   cook prod.elf stat      SYS_STAT's answer to above the break
 *
 * Each address is inside the user range, so the range check passes, but
 * the program never had it: no page there and none promised. The kernel
 * must say -1, not fault on it. Prints what each call gave.
 */
#include "ulib.h"

#define ABOVE_BRK 0xC6000000u            /* past any break this program reaches */
#define GAP       0xC2000000u            /* past the image, below USER_HEAP     */

static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void said(const char *what, int r) { print("prod: "); print(what); print(" gave "); print_int(r); print("\n"); }

int main(void) {
    char *argv[4];
    int argc = uargv(argv, 4);
    if (argc < 1) { print("usage: prod.elf write|gap|read|stat\n"); return 2; }
    const char *m = argv[0];
    if (same(m, "write")) said("write", sys_write(FD_STDOUT, (const char *)ABOVE_BRK, 16));
    else if (same(m, "gap")) said("write", sys_write(FD_STDOUT, (const char *)GAP, 16));
    else if (same(m, "read")) {
        int fd = sys_open("/HELLO.TXT", O_READ);
        said("read", sys_read(fd, (char *)ABOVE_BRK, 16));
        sys_close(fd);
    } else if (same(m, "stat")) said("stat", sys_stat("/HELLO.TXT", (ustat_t *)ABOVE_BRK));
    else { print("usage: prod.elf write|gap|read|stat\n"); return 2; }
    return 0;
}

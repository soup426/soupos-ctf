/* prod.c - prods the kernel with what a careful program would never
 * hand it, for the tests (v0.60.149): kitchen name, a poke with a fork.
 *
 *   cook prod.elf write     SYS_WRITE from above the heap's break
 *   cook prod.elf gap       SYS_WRITE from between the image and the heap
 *   cook prod.elf read      SYS_READ (a file) into above the break
 *   cook prod.elf stat      SYS_STAT's answer to above the break
 *   cook prod.elf victim &  (v0.60.150) a green window that waits; when the
 *                           desktop goes, what the calls give after
 *   cook prod.elf limits    (v0.60.150) the window calls, attacked: sizes,
 *                           a title running off its memory, pixel and event
 *                           pointers it never had, ids that are not its own
 *
 * Both wait up to a minute for the desktop. prodwin-test starts victim at
 * the console in the background before countertop (what it prints goes
 * to the serial log), and runs limits in a terminal on the desktop once
 * the victim's window is up, its answers sent to a file.
 *
 * Each address is inside the user range, so the range check passes, but
 * the program never had it: no page there and none promised. The kernel
 * must say -1, not fault on it. Prints what each call gave.
 */
#include "ulib.h"

#define ABOVE_BRK 0xC6000000u            /* past any break this program reaches */
#define GAP       0xC2000000u            /* past the image, below USER_HEAP     */
#define KERNEL    0x00100000u            /* the kernel's own memory             */

static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void said(const char *what, int r) { print("prod: "); print(what); print(" gave "); print_int(r); print("\n"); }

static int open_when_up(int w, int h, const char *title) {   /* the desktop, within a minute */
    for (int i = 0; i < 600; i++) {
        int id = sys_win_open(w, h, title);
        if (id >= 0) return id;
        sys_sleep(100);
    }
    return -1;
}

static int victim(void) {
    int id = open_when_up(100, 100, "prod victim");
    if (id < 0) { print("prod: no desktop\n"); return 1; }
    unsigned *px = malloc(100 * 100 * 4);
    for (int i = 0; i < 100 * 100; i++) px[i] = 0x0027AE60;
    sys_win_put(id, px);
    print("prod: victim window open\n");
    for (;;) {
        uwinev_t e;
        int r = sys_win_event(id, &e, 1000);
        if (r == 1 && e.type == WEV_CLOSE) { sys_win_close(id); print("prod: victim closed\n"); return 0; }
        if (r < 0) {
            said("after the desktop, put", sys_win_put(id, px));
            said("after the desktop, event", sys_win_event(id, &e, 0));
            said("after the desktop, close", sys_win_close(id));
            said("after the desktop, open", sys_win_open(10, 10, "x"));
            return 0;
        }
    }
}

static int limits(void) {
    int me = open_when_up(100, 100, "prod limits");
    if (me < 0) { print("prod: no desktop\n"); return 1; }
    said("open 0x10", sys_win_open(0, 10, "x"));
    said("open 641x10", sys_win_open(641, 10, "x"));
    said("open 10x481", sys_win_open(10, 481, "x"));
    said("open -1x10", sys_win_open(-1, 10, "x"));
    said("open, a kernel title", sys_win_open(10, 10, (const char *)KERNEL));
    /* A title whose last bytes are the last the program has: no NUL before
     * the break, and nothing promised past it. */
    char *brk = (char *)sys_sbrk(0);
    unsigned up = 4096 - ((unsigned)brk & 4095);
    char *top = (char *)sys_sbrk((int)up) + up;               /* the break, on a page boundary */
    for (int i = 1; i <= 8; i++) top[-i] = 'A';
    said("open, a title running off its memory", sys_win_open(10, 10, top - 8));
    unsigned *px = malloc(100 * 100 * 4);
    for (int i = 0; i < 100 * 100; i++) px[i] = 0x00C0392B;
    said("put from above the break", sys_win_put(me, (const unsigned *)ABOVE_BRK));
    said("put from the kernel", sys_win_put(me, (const unsigned *)KERNEL));
    char *end = (char *)sys_sbrk(0);
    said("put half past the break", sys_win_put(me, (const unsigned *)(end - 20000)));
    uwinev_t e;
    said("event into the kernel", sys_win_event(me, (uwinev_t *)KERNEL, 0));
    said("event above the break", sys_win_event(me, (uwinev_t *)ABOVE_BRK, 0));
    said("put to id 8", sys_win_put(8, px));
    said("put to id -1", sys_win_put(-1, px));
    int tried = 0, took = 0;
    for (int id = 0; id < 8; id++) {                          /* everyone else's, the victim's among them */
        if (id == me) continue;
        tried++;
        if (sys_win_put(id, px) != -1) took++;
        if (sys_win_event(id, &e, 0) != -1) took++;
        if (sys_win_close(id) != -1) took++;
    }
    print("prod: others' ids: "); print_int(tried); print(took ? " tried, some answered\n" : " tried, all -1\n");
    said("put to its own", sys_win_put(me, px));
    said("close its own", sys_win_close(me));
    said("close it again", sys_win_close(me));
    return 0;
}

int main(void) {
    char *argv[4];
    int argc = uargv(argv, 4);
    if (argc < 1) { print("usage: prod.elf write|gap|read|stat|victim|limits\n"); return 2; }
    const char *m = argv[0];
    if (same(m, "write")) said("write", sys_write(FD_STDOUT, (const char *)ABOVE_BRK, 16));
    else if (same(m, "gap")) said("write", sys_write(FD_STDOUT, (const char *)GAP, 16));
    else if (same(m, "read")) {
        int fd = sys_open("/HELLO.TXT", O_READ);
        said("read", sys_read(fd, (char *)ABOVE_BRK, 16));
        sys_close(fd);
    } else if (same(m, "stat")) said("stat", sys_stat("/HELLO.TXT", (ustat_t *)ABOVE_BRK));
    else if (same(m, "victim")) return victim();
    else if (same(m, "limits")) return limits();
    else { print("usage: prod.elf write|gap|read|stat|victim|limits\n"); return 2; }
    return 0;
}

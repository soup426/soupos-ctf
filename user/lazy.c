/* lazy.c - memory the machine does not have, yet.
 *
 * Asks sbrk for 16 MB, which is four times the per-process page cap and far
 * more than a program used to be able to get, then touches one page in
 * every sixty-four. With demand paging the sbrk is free and only the touched
 * pages ever get a frame; every one of them must read as zero before the
 * write and hold the value after.
 *
 *   cook lazy.elf           64 pages touched out of 4096 claimed
 *   cook lazy.elf greedy    touches every page: runs into the cap and is
 *                           killed, which is the honest answer, and the
 *                           shell must survive it
 *   cook lazy.elf read      has the KERNEL touch the page first: a read()
 *                           into a buffer this program never wrote to, so
 *                           the fault is taken in ring 0 and must be
 *                           answered the same way rather than panicking
 */
#include "ulib.h"

#define PAGE   4096
#define PAGES  4096                 /* 16 MB */

int main(void) {
    char arg[16];
    int n = sys_args(arg, sizeof(arg));
    int greedy = (n > 0 && arg[0] == 'g');
    int kread  = (n > 0 && arg[0] == 'r');

    char *base = (char *)sys_sbrk(PAGES * PAGE);
    if (base == (char *)-1 || base == 0) {
        eprint("LAZY sbrk-failed\n");
        return 1;
    }

    if (kread) {
        /* Two pages in, never touched by us: the first write to it is the
         * kernel's, inside the read syscall. */
        char *buf = base + 2 * PAGE;
        int fd = sys_open("README.TXT", 0);
        if (fd < 0) { eprint("LAZY no README.TXT\n"); return 4; }
        int got = sys_read(fd, buf, 64);
        sys_close(fd);
        if (got <= 0) { eprint("LAZY read failed\n"); return 5; }
        eprint("LAZY kernel wrote ");
        eprint_int(got);
        eprint(" bytes into an untouched page ok\n");
        return 0;
    }

    int step = greedy ? 1 : 64, touched = 0;
    for (int i = 0; i < PAGES; i += step) {
        char *p = base + (long)i * PAGE;
        if (p[0] != 0 || p[PAGE - 1] != 0) {      /* a fresh page reads as zero */
            eprint("LAZY FAIL page not zero at=");
            eprint_int(i);
            eprint("\n");
            return 2;
        }
        p[0] = (char)(i & 0x7F);
        p[PAGE - 1] = (char)~(i & 0x7F);
        touched++;
    }
    for (int i = 0; i < PAGES; i += step) {
        char *p = base + (long)i * PAGE;
        if (p[0] != (char)(i & 0x7F) || p[PAGE - 1] != (char)~(i & 0x7F)) {
            eprint("LAZY FAIL page lost its value at=");
            eprint_int(i);
            eprint("\n");
            return 3;
        }
    }
    eprint("LAZY touched ");
    eprint_int(touched);
    eprint(" of ");
    eprint_int(PAGES);
    eprint(" pages ok\n");
    return 0;
}

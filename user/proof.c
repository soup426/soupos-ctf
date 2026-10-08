/* proof.c - memory the machine does not have, yet.
 *
 * Asks sbrk for 16 MB, which is four times the per-process page cap and far
 * more than a program used to be able to get, then touches one page in
 * every sixty-four. With demand paging the sbrk is free and only the touched
 * pages ever get a frame; every one of them must read as zero before the
 * write and hold the value after.
 *
 *   cook proof.elf           64 pages touched out of 4096 claimed
 *   cook proof.elf greedy    touches every page of the 16 MB: 1024 stay
 *                           resident and the rest go to swap and come back,
 *                           so it COMPLETES, slowly
 *   cook proof.elf absurd    32 MB, more than the cap and the swap together:
 *                           killed when the swap is full, which is the
 *                           honest answer, and the shell must survive it
 *   cook proof.elf hold      touches 1500 pages, then stays alive for 4 s
 *                           doing nothing, so `ps` can be asked what it
 *                           holds: 1024 resident (the cap) and the rest on
 *                           disk
 *   cook proof.elf read      has the KERNEL touch the page first: a read()
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
    int greedy = (n > 0 && (arg[0] == 'g' || arg[0] == 'a'));
    int kread  = (n > 0 && arg[0] == 'r');
    int hold   = (n > 0 && arg[0] == 'h');
    int pages  = (n > 0 && arg[0] == 'a') ? 2 * PAGES : PAGES;

    char *base = (char *)sys_sbrk(pages * PAGE);
    if (base == (char *)-1 || base == 0) {
        eprint("LAZY sbrk-failed\n");
        return 1;
    }

    if (hold) {
        for (int i = 0; i < 1500; i++) base[(long)i * PAGE] = (char)(i & 0x7F);
        eprint("LAZY holding 1500 pages\n");
        unsigned until = sys_ticks() + 400;
        while (sys_ticks() < until) sys_yield();
        for (int i = 0; i < 1500; i++)
            if (base[(long)i * PAGE] != (char)(i & 0x7F)) { eprint("LAZY FAIL held page\n"); return 6; }
        eprint("LAZY held 1500 pages ok\n");
        return 0;
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
    for (int i = 0; i < pages; i += step) {
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
    for (int i = 0; i < pages; i += step) {
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
    eprint_int(pages);
    eprint(" pages ok\n");
    return 0;
}

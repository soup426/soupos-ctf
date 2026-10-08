/* fridge.c - a working set that fits, and a sweep that does not.
 *
 * 400 hot pages are touched every round; each round also touches 400 cold
 * pages it will never touch again. The cap is 1024 pages, of which the image
 * and stack take seven, so hot plus one round's sweep (800) fits with room.
 * (The first version used 512 + 512, a hair over the 1017 the heap really
 * gets, and a loop just larger than memory is LRU's worst case: a model on
 * the host reproduced the kernel's numbers exactly, which is how that was
 * told apart from a bug.) The hot set always fits - a good victim choice keeps it and evicts the
 * cold pages, so the kernel's "back in" count on exit stays near zero. A bad
 * one (FIFO: the hot pages are always the oldest mapped) evicts the hot set
 * every round and fetches it straight back.
 *
 * Every page holds a value derived from its index, checked on every touch,
 * so a page that comes back wrong from swap is caught here.
 */
#include "ulib.h"

#define PAGE   4096
#define HOT    400
#define COLD   400
#define ROUNDS 8

int main(void) {
    char *hot  = (char *)sys_sbrk(HOT * PAGE);
    char *cold = (char *)sys_sbrk(ROUNDS * COLD * PAGE);
    if (hot == (char *)-1 || cold == (char *)-1) { eprint("HOTCOLD sbrk-failed\n"); return 1; }

    for (int i = 0; i < HOT; i++) hot[(long)i * PAGE] = (char)(i * 7 + 1);
    for (int r = 0; r < ROUNDS; r++) {
        for (int i = 0; i < HOT; i++) {                 /* the working set, every round */
            char *p = hot + (long)i * PAGE;
            if (*p != (char)(i * 7 + 1)) { eprint("HOTCOLD FAIL hot page lost its value\n"); return 2; }
            p[1] = (char)r;
        }
        for (int i = 0; i < COLD; i++) {                /* a sweep it never revisits */
            long k = (long)r * COLD + i;
            cold[k * PAGE] = (char)(k & 0x7F);
            if (cold[k * PAGE] != (char)(k & 0x7F)) { eprint("HOTCOLD FAIL cold page\n"); return 3; }
        }
    }
    eprint("HOTCOLD done ok\n");
    return 0;
}

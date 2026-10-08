/* measure.c - catches two processes being handed the same physical page.
 *
 * Takes a one-character tag, claims a block of heap with sbrk, fills every
 * byte with that tag, yields enough for the other copies to do the same, then
 * checks its memory still says what it wrote. If the physical page allocator
 * ever hands the same frame to two processes, whoever writes second wins and
 * the other one sees the wrong tag here.
 *
 * Run several at once:
 *   cook measure.elf A &
 *   cook measure.elf B &
 *   cook measure.elf C
 */
#include "ulib.h"

#define PAGES  8
#define BYTES  (PAGES * 4096)

int main(void) {
    char tag[16];
    int n = sys_args(tag, sizeof(tag));
    char t = (n > 0 && tag[0]) ? tag[0] : '?';

    /* Line up with the other copies. Launching them by typing staggers them by
     * a second or so each, which is long enough that their allocations would
     * barely overlap; waiting for a common tick boundary puts every copy in
     * the allocator at the same moment, which is the point of the test. */
    unsigned target = ((sys_ticks() / 50u) + 1u) * 50u;
    while (sys_ticks() < target) sys_yield();

    char *p = (char *)sys_sbrk(BYTES);
    if (p == (char *)-1 || p == 0) {
        eprint("MEMTEST sbrk-failed\n");
        return 1;
    }

    for (int i = 0; i < BYTES; i++) p[i] = t;

    /* Let the other copies allocate and write while we hold ours. */
    for (int k = 0; k < 40; k++) sys_yield();

    for (int i = 0; i < BYTES; i++) {
        if (p[i] != t) {
            eprint("MEMTEST FAIL tag=");
            eprint(tag);
            eprint(" at=");
            eprint_int(i);
            eprint(" saw=");
            eprint_int((int)p[i]);
            eprint("\n");
            return 2;
        }
    }

    eprint("MEMTEST OK tag=");
    eprint(tag);
    eprint("\n");
    return 0;
}

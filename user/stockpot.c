/* stockpot.c - a stack that grows, and one that grows too far.
 *
 * Every level of recursion keeps a kilobyte of its own on the stack and
 * writes all of it, so each level really does use the memory and the
 * compiler cannot fold the frames away.
 *
 *   cook stockpot.elf           600 levels, about 600 KB: far past the 16 KB the
 *                           stack used to be, so it only works if the stack
 *                           grows on demand. Checks every frame on the way
 *                           back up.
 *   cook stockpot.elf forever   recurses until the guard page stops it: the
 *                           kernel must say "stack overflow" and kill this
 *                           program, and the shell must carry on.
 */
#include "ulib.h"

static int deepest;

static int dive(int level, int limit) {
    volatile char frame[1024];
    for (int i = 0; i < (int)sizeof(frame); i++) frame[i] = (char)(level + i);
    if (level > deepest) deepest = level;
    int below = (limit < 0 || level < limit) ? dive(level + 1, limit) : level;
    /* On the way back: this frame must still hold what it was given. */
    for (int i = 0; i < (int)sizeof(frame); i++)
        if (frame[i] != (char)(level + i)) return -1;
    return below;
}

int main(void) {
    char arg[16];
    int n = sys_args(arg, sizeof(arg));
    int forever = (n > 0 && arg[0] == 'f');

    int got = dive(1, forever ? -1 : 600);
    if (got < 0) {
        eprint("DEEP FAIL a frame lost its contents\n");
        return 2;
    }
    eprint("DEEP reached ");
    eprint_int(deepest);
    eprint(" levels ok\n");
    return 0;
}

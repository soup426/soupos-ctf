/* main.c - Mincecraft (v0.60.192): a voxel sandbox in a food world.
 *
 *   cook mincecraft.elf                 play (from a terminal on the desktop)
 *   cook mincecraft.elf -gen -seed N    make world N, print its hash, end
 */
#include "../ulib.h"
#include "world.h"

static void eprint_u(unsigned v) {
    char b[12]; int n = 11; b[n] = 0;
    do { b[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    eprint(b + n);
}
static unsigned parse_u(const char *s) { unsigned v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned)(*s++ - '0'); return v; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

int main(void) {
    char *argv[12];
    int argc = uargv(argv, 12), gen_only = 0, flat = 0, seeded = 0;
    unsigned seed = 0;
    for (int i = 0; i < argc; i++) {
        if (same(argv[i], "-gen")) gen_only = 1;
        else if (same(argv[i], "-flat")) flat = 1;
        else if (same(argv[i], "-seed") && i + 1 < argc) { seed = parse_u(argv[++i]); seeded = 1; }
    }
    if (world_alloc() < 0) { print("Mincecraft: no memory for the world\n"); return 1; }
    if (!seeded) sys_random(&seed, 4);
    if (flat) world_flat(); else world_gen(seed);
    if (gen_only) {
        eprint("[mincecraft] world seed="); eprint_u(seed);
        eprint(" hash="); eprint_u(world_hash());
        eprint(" top="); eprint_u((unsigned)world_top(64, 64));
        eprint("\n");
        return 0;
    }
    print("Mincecraft: play comes in a later version\n");
    return 1;
}

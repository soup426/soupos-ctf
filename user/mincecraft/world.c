/* world.c - the grid and its generation (v0.60.192). scripts/mincecraft_gen.py
 * is this file again in Python for the tests: change both together. */
#include "../ulib.h"
#include "world.h"
unsigned char *world;
int world_alloc(void) {
    void *p = sys_sbrk(WCELLS);
    if (p == (void *)-1) return -1;
    world = (unsigned char *)p;
    return 0;
}
static unsigned hash2(int x, int z, unsigned seed) {
    unsigned h = seed ^ ((unsigned)x * 0x27d4eb2du) ^ ((unsigned)z * 0x165667b1u);
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
    return h;
}
static int noise(int x, int z, int s, unsigned seed) {           /* 0..255, x and z >= 0 */
    int x0 = x / s, z0 = z / s, fx = x % s, fz = z % s;
    int v00 = (int)(hash2(x0, z0, seed) & 255),     v10 = (int)(hash2(x0 + 1, z0, seed) & 255);
    int v01 = (int)(hash2(x0, z0 + 1, seed) & 255), v11 = (int)(hash2(x0 + 1, z0 + 1, seed) & 255);
    int a = v00 * (s - fx) + v10 * fx, b = v01 * (s - fx) + v11 * fx;
    return (a * (s - fz) + b * fz) / (s * s);
}
/* Three octaves (32, 16, 8), weighted 3:2:1, then spread from the middle
 * (v0.60.198): value noise piles up around 128, which gave puddles for
 * lakes and a tenth of the world in beach; d|d| pushes the ends out into
 * lakes and hills. Held to 5..50 solid blocks, so a broccoli on a peak
 * still fits under the ceiling. */
int world_height(int x, int z, unsigned seed) {
    int n = (noise(x, z, 32, seed) * 3 + noise(x, z, 16, seed ^ 0x85ebca6bu) * 2 + noise(x, z, 8, seed ^ 0x9e3779b9u)) / 6;
    int d = n - 128, h = 29 + d * (d < 0 ? -d : d) / 640 + d / 16;
    return h < 5 ? 5 : h > 50 ? 50 : h;
}
void world_gen(unsigned seed) {
    for (int z = 0; z < WZ; z++) for (int x = 0; x < WX; x++) {
        int edge = x == 0 || z == 0 || x == WX - 1 || z == WZ - 1;
        int h = world_height(x, z, seed);
        int top = h <= SEA ? B_SUGAR : B_CRUST;          /* sugar under the broth and at its edge */
        for (int y = 0; y < WY; y++) {
            int b;
            if (edge || y == 0) b = B_CASTIRON;
            else if (y < h - 4) {
                unsigned r = hash2(x + z * WX, y, seed ^ 0x51ed27u);
                b = r % 97 == 0 ? B_PEPPERCORN : (y < 12 && r % 211 == 1) ? B_BUTTER : B_PARMESAN;
            }
            else if (y < h - 1) b = top == B_SUGAR ? B_SUGAR : B_CRUMB;
            else if (y == h - 1) b = top;
            else if (y < SEA) b = B_BROTH;
            else b = B_AIR;
            world[(y * WZ + z) * WX + x] = (unsigned char)b;
        }
    }
    for (int z = 3; z < WZ - 3; z++) for (int x = 3; x < WX - 3; x++) {   /* broccoli */
        int h = world_height(x, z, seed);
        if (h <= SEA) continue;
        unsigned r = hash2(x, z, seed ^ 0x7f4a7c15u);
        if (r % 60) continue;
        int t = 4 + (int)((r >> 8) % 3), cy;
        for (int y = h; y < h + t; y++) world_set(x, y, z, B_STALK);
        cy = h + t;
        for (int dy = -2; dy <= 2; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy + dz * dz <= 5 && world_get(x + dx, cy + dy, z + dz) == B_AIR)
                world_set(x + dx, cy + dy, z + dz, B_FLORET);
    }
}
void world_flat(void) {
    for (int y = 0; y < WY; y++) for (int z = 0; z < WZ; z++) for (int x = 0; x < WX; x++) {
        int edge = x == 0 || z == 0 || x == WX - 1 || z == WZ - 1;
        int pool = x >= 60 && x < 68 && z >= 72 && z < 80 && (y == 9 || y == 10);   /* broth, two deep (v0.60.199) */
        world[(y * WZ + z) * WX + x] = (unsigned char)(edge || y == 0 ? B_CASTIRON : pool ? B_BROTH : y < 10 ? B_PARMESAN : y == 10 ? B_CRUST : B_AIR);
    }
}
unsigned world_hash(void) {
    unsigned h = 2166136261u;
    for (int i = 0; i < WCELLS; i++) { h ^= world[i]; h *= 16777619u; }
    return h;
}
int world_top(int x, int z) {
    for (int y = WY - 1; y >= 0; y--) if (blocks[world_get(x, y, z)].solid) return y;
    return -1;
}

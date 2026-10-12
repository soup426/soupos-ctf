/* world.h - the block grid (v0.60.192): 128 x 128 columns, 64 high, a byte a block. */
#pragma once
#include "blocks.h"
#define WX 128
#define WZ 128
#define WY 64
#define WCELLS (WX * WZ * WY)
#define SEA 26                     /* broth fills open cells below this */
extern unsigned char *world;       /* (y * WZ + z) * WX + x */
static inline int world_get(int x, int y, int z) {
    if (x < 0 || x >= WX || z < 0 || z >= WZ || y < 0) return B_CASTIRON;
    if (y >= WY) return B_AIR;
    return world[(y * WZ + z) * WX + x];
}
static inline void world_set(int x, int y, int z, int id) {
    if (x >= 0 && x < WX && z >= 0 && z < WZ && y >= 0 && y < WY) world[(y * WZ + z) * WX + x] = (unsigned char)id;
}
int      world_alloc(void);                  /* 0, or -1 */
int      world_height(int x, int z, unsigned seed);
void     world_gen(unsigned seed);
void     world_flat(void);                   /* for the tests: crust at y 10 */
unsigned world_hash(void);                   /* FNV-1a 32 over the grid */
int      world_top(int x, int z);            /* highest solid y, or -1 */

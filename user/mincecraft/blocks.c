/* blocks.c - the block table and its 8x8 textures, made from a hash of
 * (block, face, texel) so there is no image data (v0.60.192). */
#include "blocks.h"
const block_t blocks[B_COUNT] = {
    [B_AIR]        = { "air",            0x000000, 0x000000,   0, 0,  0 },
    [B_CRUST]      = { "crust",          0xC8873A, 0x8A5A22,  70, 1,  6 },
    [B_CRUMB]      = { "crumb",          0xEAD9B0, 0xC9B489,  60, 1,  6 },
    [B_PARMESAN]   = { "parmesan",       0xEFE8C8, 0xD4C99A,  90, 1, 20 },
    [B_CASTIRON]   = { "cast iron",      0x2A2A2E, 0x18181B,  80, 1,  0 },
    [B_BROTH]      = { "broth",          0xC8902E, 0xB07A20,  40, 0,  0 },
    [B_SUGAR]      = { "sugar",          0xFAFAF5, 0xDCDCE6,  50, 1,  5 },
    [B_STALK]      = { "broccoli stalk", 0xA9C77A, 0x86A65A,  80, 1, 12 },
    [B_FLORET]     = { "floret",         0x2F6B2A, 0x1D4A1A, 110, 1,  3 },
    [B_PEPPERCORN] = { "peppercorn",     0xEFE8C8, 0x1A1410,  40, 1, 24 },
    [B_BUTTER]     = { "butter",         0xF6DC6B, 0xEBCB52,  30, 1, 10 },
    [B_TOAST]      = { "toast",          0xD9A55B, 0x8B5A2B,  60, 1,  8 },   /* (v0.60.215) */
    [B_GRATER]     = { "cheese grater",  0xB8BCC4, 0x2A2D33,   0, 1, 15 },
    [B_TEALIGHT]   = { "tealight",       0xF2F2EE, 0xFFB43C,   0, 1,  2 },
};
static unsigned tex_hash(int id, int face, int tx, int ty) {
    unsigned h = (unsigned)id * 73856093u ^ (unsigned)face * 19349663u
               ^ (unsigned)tx * 83492791u ^ (unsigned)ty * 2654435761u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h & 255;
}
unsigned block_texel(int id, int face, int tx, int ty) {
    if (id == B_CRUST && (face == FACE_BOTTOM || (face == FACE_SIDE && ty >= 2))) id = B_CRUMB;   /* crust on top, crumb inside */
    const block_t *b = &blocks[id];
    if (id == B_STALK && face != FACE_SIDE) {                     /* rings on the cut ends */
        int d = (2 * tx - 7) * (2 * tx - 7) + (2 * ty - 7) * (2 * ty - 7);
        return d >= 18 && d <= 34 ? b->alt : b->base;
    }
    if (id == B_GRATER) return (tx % 3 == 1 && ty % 3 == 1) ? b->alt : b->base;       /* rows of holes */
    if (id == B_TEALIGHT) {                                                         /* wax, a flame on top */
        if (face == FACE_TOP) { int d = (2 * tx - 7) * (2 * tx - 7) + (2 * ty - 7) * (2 * ty - 7); return d <= 10 ? b->alt : b->base; }
        return ty < 2 ? 0xC8C8C0u : b->base;
    }
    return tex_hash(id, face, tx, ty) < b->speck ? b->alt : b->base;
}

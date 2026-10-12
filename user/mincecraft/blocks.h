/* blocks.h - what the world is made of (v0.60.192). */
#pragma once
enum { B_AIR, B_CRUST, B_CRUMB, B_PARMESAN, B_CASTIRON, B_BROTH, B_SUGAR,
       B_STALK, B_FLORET, B_PEPPERCORN, B_BUTTER,
       B_TOAST, B_GRATER, B_TEALIGHT,          /* (v0.60.215) crafted, never found */
       B_COUNT };
#define B_FOUND 11                             /* the blocks before crafting: a version 1 or 2 file's counts */
enum { FACE_TOP, FACE_BOTTOM, FACE_SIDE };
typedef struct {
    const char   *name;
    unsigned      base, alt;   /* 0xRRGGBB: the texture's two colours        */
    unsigned char speck;       /* alt where the texel's hash is below this   */
    unsigned char solid;       /* collides and stops rays                    */
    unsigned char hardness;    /* ticks held to break it; 0: never           */
} block_t;
extern const block_t blocks[B_COUNT];
unsigned block_texel(int id, int face, int tx, int ty);   /* tx, ty 0..7 */

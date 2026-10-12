/* render.c - one ray a pixel at 320x240, doubled into the 640x480 body
 * (v0.60.193). Yaw 0 looks along +z; right is +x there. Faces are shaded by
 * axis, the last 8 blocks fade into the horizon's colour, broth tints what
 * is under it, darker the deeper, its surface simmering (v0.60.199). */
#include "../ulib.h"
#include "render.h"
#include "ray.h"
#include "world.h"
#define RW 320
#define RH 240
#define VIEW 24                          /* blocks */
#define FOG0 (VIEW - 8)
#define KPIX 287                         /* tan(35 degrees) / 160, 16.16: a 70-degree view */
static unsigned *rbuf;
static unsigned frame;                   /* for the broth's simmer */
#define BROTH_COL 0xB0661C               /* deeper than its texture: soup, not sand */
#define OUTLINE   0x1A1208               /* the edges of the block aimed at */
int render_alloc(void) {
    void *p = sys_sbrk(RW * RH * 4);
    if (p == (void *)-1) return -1;
    rbuf = (unsigned *)p;
    return 0;
}
static unsigned shade(unsigned c, int k) {               /* k/256 */
    unsigned r = ((c >> 16 & 255) * (unsigned)k) >> 8, g = ((c >> 8 & 255) * (unsigned)k) >> 8, b = ((c & 255) * (unsigned)k) >> 8;
    return r << 16 | g << 8 | b;
}
static unsigned mix(unsigned a, unsigned b, int k) {     /* k/256 of b */
    int ra = a >> 16 & 255, ga = a >> 8 & 255, ba = a & 255, rb = b >> 16 & 255, gb = b >> 8 & 255, bb = b & 255;
    return (unsigned)(ra + ((rb - ra) * k >> 8)) << 16 | (unsigned)(ga + ((gb - ga) * k >> 8)) << 8 | (unsigned)(ba + ((bb - ba) * k >> 8));
}
void cam_forward(const cam_t *c, fx *dx, fx *dy, fx *dz) {
    fx sy = fx_sin(c->yaw), cy = fx_cos(c->yaw), sp = fx_sin(c->pitch), cp = fx_cos(c->pitch);
    *dx = fx_mul(sy, cp); *dy = sp; *dz = fx_mul(cy, cp);
}
void render(const cam_t *c, unsigned *body, int light, unsigned sky, const ray_t *tg, int crack) {
    fx sy = fx_sin(c->yaw), cy = fx_cos(c->yaw), sp = fx_sin(c->pitch), cp = fx_cos(c->pitch);
    fx fx_ = fx_mul(sy, cp), fy = sp, fz = fx_mul(cy, cp);       /* forward */
    fx rx = cy, rz = -sy;                                         /* right (ry 0) */
    fx ux = -fx_mul(sy, sp), uy = cp, uz = -fx_mul(cy, sp);       /* up */
    /* The sky lightens toward the horizon (v0.60.199), and distance fades
     * into that horizon colour; straight up and above 0.4 it is the sky. */
    unsigned horizon = mix(sky, 0xFFFFFF, 90 * light >> 8), bcol = shade(BROTH_COL, light), bub = shade(0xF0C070, light);
    frame++;
    for (int py = 0; py < RH; py++) {
        fx v = (RH / 2 - py) * KPIX, h0 = -(RW / 2) * KPIX;
        fx dx = fx_ + fx_mul(ux, v) + fx_mul(rx, h0), dy = fy + fx_mul(uy, v), dz = fz + fx_mul(uz, v) + fx_mul(rz, h0);
        fx stx = fx_mul(rx, KPIX), stz = fx_mul(rz, KPIX);
        int sk = dy <= 0 ? 0 : dy >= 26214 ? 256 : dy / 102;       /* the row's sky, by how far up it looks */
        unsigned rowsky = mix(horizon, sky, sk);
        for (int px = 0; px < RW; px++, dx += stx, dz += stz) {
            ray_t r;
            unsigned col;
            fx dist = -1;
            ray_cast(c->x, c->y, c->z, dx, dy, dz, VIEW << 16, &r);
            if (!r.hit) col = rowsky;
            else {
                int b = world_get(r.x, r.y, r.z);
                int face = r.axis == 1 ? (r.step < 0 ? FACE_TOP : FACE_BOTTOM) : FACE_SIDE;
                int tx = r.u >> 13, ty = face == FACE_SIDE ? 7 - (r.v >> 13) : r.v >> 13;
                int k = face == FACE_TOP ? 256 : face == FACE_BOTTOM ? 128 : r.axis == 0 ? 205 : 166;
                col = shade(block_texel(b, face, tx, ty), k * light >> 8);
                if (tg->hit && r.x == tg->x && r.y == tg->y && r.z == tg->z) {   /* the block aimed at (v0.60.200) */
                    if (r.u < 0x0C00 || r.u > 0xF400 || r.v < 0x0C00 || r.v > 0xF400) col = shade(OUTLINE, light);
                    else if (crack) {                             /* cracks: scattered texels darken as it gives */
                        unsigned h = (unsigned)tx * 2654435761u ^ (unsigned)ty * 40503u ^ (unsigned)face * 2246822519u;
                        h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;
                        if ((int)(h & 255) < crack) col = shade(col, 110);
                    }
                }
                dist = r.t;
            }
            if (r.broth_t >= 0) {                                 /* broth: darker the deeper (v0.60.199) */
                int depth = dist >= 0 ? (dist - r.broth_t) >> 12 : 4096;   /* 16ths of a block */
                int k = 110 + depth * 4;
                unsigned bc = bcol;
                if (r.broth_t > 0) {                              /* its surface simmers: a bubble here and there */
                    unsigned hx = (unsigned)((c->x + fx_mul(dx, r.broth_t)) >> 13), hz = (unsigned)((c->z + fx_mul(dz, r.broth_t)) >> 13);
                    unsigned h = hx * 73856093u ^ hz * 19349663u ^ (unsigned)(frame >> 3) * 83492791u;
                    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                    if ((h & 63) == 0) bc = bub;
                    if (dist < 0) dist = r.broth_t;
                }
                col = mix(col, bc, k > 230 ? 230 : k);
            }
            if (dist > (FOG0 << 16)) { int f = (dist - (FOG0 << 16)) >> 11; col = mix(col, horizon, f > 256 ? 256 : f); }
            rbuf[py * RW + px] = col;
        }
    }
    for (int py = 0; py < RH; py++) {                            /* doubled */
        unsigned *a = body + (2 * py) * BODY_W, *b2 = a + BODY_W;
        const unsigned *s = rbuf + py * RW;
        for (int px = 0; px < RW; px++) { unsigned c2 = s[px]; a[2 * px] = a[2 * px + 1] = b2[2 * px] = b2[2 * px + 1] = c2; }
    }
}

/* ray.c - the DDA (v0.60.193). Each step goes to the nearest cell
 * boundary on whichever axis is closest. A direction component near zero
 * would overflow 1/d in 16.16, so its step is held at BIG and never taken. */
#include "ray.h"
#include "world.h"
#define BIG (1 << 30)
static inline fx tdelta(fx d) { if (d < 0) d = -d; return d < 64 ? BIG : (fx)((1LL << 32) / d); }
static inline fx tfirst(fx o, fx d, fx td) {
    if (td == BIG) return BIG;
    fx frac = d > 0 ? FX_ONE - (o & 0xFFFF) : (o & 0xFFFF);
    return (fx)(((long long)frac * td) >> 16);
}
void ray_cast(fx ox, fx oy, fx oz, fx dx, fx dy, fx dz, fx maxt, ray_t *r) {
    int cx = ox >> 16, cy = oy >> 16, cz = oz >> 16;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
    fx tdx = tdelta(dx), tdy = tdelta(dy), tdz = tdelta(dz);
    fx tmx = tfirst(ox, dx, tdx), tmy = tfirst(oy, dy, tdy), tmz = tfirst(oz, dz, tdz);
    r->hit = 0;
    r->broth_t = world_get(cx, cy, cz) == B_BROTH ? 0 : -1;
    for (;;) {
        int px = cx, py = cy, pz = cz, axis;
        fx t;
        if (tmx <= tmy && tmx <= tmz) { t = tmx; cx += sx; tmx += tdx; axis = 0; }
        else if (tmy <= tmz)          { t = tmy; cy += sy; tmy += tdy; axis = 1; }
        else                          { t = tmz; cz += sz; tmz += tdz; axis = 2; }
        if (t > maxt) return;
        if (cy >= WY && sy > 0) return;                       /* above the world, rising: sky */
        int b = world_get(cx, cy, cz);
        if (b == B_BROTH) { if (r->broth_t < 0) r->broth_t = t; continue; }
        if (!blocks[b].solid) continue;
        r->hit = 1; r->x = cx; r->y = cy; r->z = cz; r->px = px; r->py = py; r->pz = pz;
        r->axis = axis; r->step = axis == 0 ? sx : axis == 1 ? sy : sz; r->t = t;
        fx hx = ox + fx_mul(dx, t), hy = oy + fx_mul(dy, t), hz = oz + fx_mul(dz, t);
        if (axis == 0)      { r->u = hz & 0xFFFF; r->v = hy & 0xFFFF; }
        else if (axis == 1) { r->u = hx & 0xFFFF; r->v = hz & 0xFFFF; }
        else                { r->u = hx & 0xFFFF; r->v = hy & 0xFFFF; }
        return;
    }
}

/* ray.h - a ray through the block grid, Amanatides and Woo (v0.60.193). */
#pragma once
#include "fix.h"
typedef struct ray {
    int hit;            /* 1: a solid block; 0: sky or out of reach        */
    int x, y, z;        /* the block hit                                   */
    int px, py, pz;     /* the cell before it, where a placed block goes   */
    int axis, step;     /* the face crossed: axis 0 x, 1 y, 2 z; its step  */
    fx  t;              /* how far along d                                 */
    fx  u, v;           /* the hit point across the face, 0..FX_ONE-1      */
    fx  broth_t;        /* where the ray entered broth, or -1              */
} ray_t;
void ray_cast(fx ox, fx oy, fx oz, fx dx, fx dy, fx dz, fx maxt, ray_t *r);

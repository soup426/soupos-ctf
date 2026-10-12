/* player.c - movement at a fixed 20 ticks a second (v0.60.194): walking,
 * gravity, jumping, broth, and collision one axis at a time. No move is a
 * block or more in a tick, so the box can only newly touch its edge cells. */
#include "player.h"
#include "world.h"
#define WALK  14090          /* 0.215 blocks a tick, 4.3 a second */
#define GRAV  5243           /* 0.08 a tick, each tick */
#define JUMP  27525          /* 0.42: about 1.3 blocks up */
#define TERM  58982          /* 0.9 a tick at most, falling */
#define SWIM  6554           /* 0.1: space in broth */
#define LOOK  8              /* angle units a tick */
static int solid_at(int x, int y, int z) { return blocks[world_get(x, y, z)].solid; }
static int box_hits(fx x, fx y, fx z) {
    int x0 = (x - P_HW) >> 16, x1 = (x + P_HW - 1) >> 16, y0 = y >> 16, y1 = (y + P_H - 1) >> 16;
    int z0 = (z - P_HW) >> 16, z1 = (z + P_HW - 1) >> 16;
    for (int yy = y0; yy <= y1; yy++) for (int zz = z0; zz <= z1; zz++) for (int xx = x0; xx <= x1; xx++)
        if (solid_at(xx, yy, zz)) return 1;
    return 0;
}
static void move_x(player_t *p, fx d) {
    fx n = p->x + d;
    if (!d) return;
    if (!box_hits(n, p->y, p->z)) { p->x = n; return; }
    p->x = d > 0 ? ((((n + P_HW - 1) >> 16) << 16) - P_HW) : (((((n - P_HW) >> 16) + 1) << 16) + P_HW);
}
static void move_z(player_t *p, fx d) {
    fx n = p->z + d;
    if (!d) return;
    if (!box_hits(p->x, p->y, n)) { p->z = n; return; }
    p->z = d > 0 ? ((((n + P_HW - 1) >> 16) << 16) - P_HW) : (((((n - P_HW) >> 16) + 1) << 16) + P_HW);
}
static void move_y(player_t *p, fx d) {
    fx n = p->y + d;
    if (!box_hits(p->x, n, p->z)) { p->y = n; p->ground = 0; return; }
    if (d < 0) { p->y = ((n >> 16) + 1) << 16; p->ground = 1; }
    else p->y = (((n + P_H - 1) >> 16) << 16) - P_H;
    p->vy = 0;
}
void player_spawn(player_t *p) {
    int x = WX / 2, z = WZ / 2;
    while (x < WX - 2 && world_get(x, world_top(x, z) + 1, z) == B_BROTH) x++;   /* a dry column */
    for (int i = 0; i < (int)sizeof(*p); i++) ((char *)p)[i] = 0;
    p->x = (x << 16) + FX_ONE / 2; p->z = (z << 16) + FX_ONE / 2;
    p->y = (world_top(x, z) + 1) << 16;
    p->ground = 1; p->bx = -1;
}
void player_eye(const player_t *p, cam_t *c) { c->x = p->x; c->y = p->y + P_EYE; c->z = p->z; c->yaw = p->yaw; c->pitch = p->pitch; }
int player_tick(player_t *p, const input_t *in, const ray_t *tg) {
    (void)tg;                                                     /* breaking: Task 6 */
    p->yaw = (p->yaw + in->turn * LOOK) & 1023;
    p->pitch += in->tilt * LOOK;
    if (p->pitch > 227) p->pitch = 227;
    if (p->pitch < -227) p->pitch = -227;
    int wet = world_get(p->x >> 16, (p->y + FX_ONE / 2) >> 16, p->z >> 16) == B_BROTH;
    fx sp = wet ? WALK / 2 : WALK, s = fx_sin(p->yaw), c = fx_cos(p->yaw);
    move_x(p, fx_mul(s, in->fwd * sp) + fx_mul(c, in->side * sp));
    move_z(p, fx_mul(c, in->fwd * sp) - fx_mul(s, in->side * sp));
    if (wet) { p->vy -= GRAV / 2; if (in->jump) p->vy = SWIM; if (p->vy < -SWIM) p->vy = -SWIM; }
    else { p->vy -= GRAV; if (in->jump && p->ground) p->vy = JUMP; if (p->vy < -TERM) p->vy = -TERM; }
    move_y(p, p->vy);
    return 0;
}

/* player.c - movement at a fixed 20 ticks a second (v0.60.194): walking,
 * breaking by hardness and placing (v0.60.195),
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
    int x = WX / 2, z = WZ / 2;                                   /* the dry column nearest the centre (v0.60.198) */
    for (int r = 0; r < WX / 2 - 2 && world_get(x, world_top(x, z) + 1, z) == B_BROTH; r++)
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
            int cx = WX / 2 + dx, cz = WZ / 2 + dz;
            if ((dx == -r || dx == r || dz == -r || dz == r) && world_get(x, world_top(x, z) + 1, z) == B_BROTH
                    && world_get(cx, world_top(cx, cz) + 1, cz) != B_BROTH) { x = cx; z = cz; }
        }
    for (int i = 0; i < (int)sizeof(*p); i++) ((char *)p)[i] = 0;
    p->x = (x << 16) + FX_ONE / 2; p->z = (z << 16) + FX_ONE / 2;
    p->y = (world_top(x, z) + 1) << 16;
    p->ground = 1; p->bx = -1;
    p->hp = P_HEARTS; p->fall_top = p->y;
}
void player_eye(const player_t *p, cam_t *c) { c->x = p->x; c->y = p->y + P_EYE; c->z = p->z; c->yaw = p->yaw; c->pitch = p->pitch; }
int player_tick(player_t *p, const input_t *in, const ray_t *tg) {
    int broke = 0, b;                                             /* breaking (v0.60.195), first: */
    if (in->brk && tg->hit && blocks[b = world_get(tg->x, tg->y, tg->z)].hardness) {   /* a fall starts this tick */
        if (tg->x != p->bx || tg->y != p->by || tg->z != p->bz) { p->bx = tg->x; p->by = tg->y; p->bz = tg->z; p->bn = 0; }
        if (++p->bn >= blocks[b].hardness) {
            world_set(tg->x, tg->y, tg->z, B_AIR);
            if (p->inv[b] < 999) p->inv[b]++;
            p->bn = 0; broke = b;
        }
    } else p->bn = 0;
    p->yaw = (p->yaw + in->turn * LOOK) & 1023;
    p->pitch += in->tilt * LOOK;
    if (p->pitch > 227) p->pitch = 227;
    if (p->pitch < -227) p->pitch = -227;
    int wet = world_get(p->x >> 16, (p->y + FX_ONE / 2) >> 16, p->z >> 16) == B_BROTH;
    p->wet = wet;
    fx sp = wet ? WALK / 2 : WALK, s = fx_sin(p->yaw), c = fx_cos(p->yaw);
    move_x(p, fx_mul(s, in->fwd * sp) + fx_mul(c, in->side * sp));
    move_z(p, fx_mul(c, in->fwd * sp) - fx_mul(s, in->side * sp));
    if (wet) { p->vy -= GRAV / 2; if (in->jump) p->vy = SWIM; if (p->vy < -SWIM) p->vy = -SWIM; }
    else { p->vy -= GRAV; if (in->jump && p->ground) p->vy = JUMP; if (p->vy < -TERM) p->vy = -TERM; }
    int was_ground = p->ground;
    move_y(p, p->vy);
    /* Falling (v0.60.221): a heart a block past P_SAFE, counted from the
     * highest the feet were; broth breaks a fall, so it starts it again. */
    p->hurt = 0;
    if (wet || world_get(p->x >> 16, (p->y + FX_ONE / 2) >> 16, p->z >> 16) == B_BROTH) p->fall_top = p->y;
    else if (!p->ground) { if (p->y > p->fall_top) p->fall_top = p->y; }
    else {
        if (!was_ground) {
            int d = (p->fall_top - p->y) >> 16;
            if (d > P_SAFE) { p->hurt = d - P_SAFE; p->hp = p->hp > p->hurt ? p->hp - p->hurt : 0; }
        }
        p->fall_top = p->y;
    }
    return broke;
}
/* The hotbar's block on the face the crosshair is on (v0.60.195): never
 * into the cook's own box, never without one in hand. */
int player_place(player_t *p, const ray_t *tg, int id) {
    if (!tg->hit || id <= 0 || id >= B_COUNT || p->inv[id] == 0 || tg->py >= WY) return 0;
    int c = world_get(tg->px, tg->py, tg->pz);
    if (c != B_AIR && c != B_BROTH) return 0;
    int x0 = (p->x - P_HW) >> 16, x1 = (p->x + P_HW - 1) >> 16, y0 = p->y >> 16, y1 = (p->y + P_H - 1) >> 16;
    int z0 = (p->z - P_HW) >> 16, z1 = (p->z + P_HW - 1) >> 16;
    if (tg->px >= x0 && tg->px <= x1 && tg->py >= y0 && tg->py <= y1 && tg->pz >= z0 && tg->pz <= z1) return 0;
    world_set(tg->px, tg->py, tg->pz, id);
    p->inv[id]--;
    return 1;
}
int hotbar(const player_t *p, int ids[9]) {                       /* the kinds held, in id order */
    int n = 0;
    for (int id = 1; id < B_COUNT && n < 9; id++) if (p->inv[id]) ids[n++] = id;
    return n;
}
/* Eating (v0.60.224): sugar a heart, butter two, a floret three; never
 * past ten, and nothing eaten at ten. */
int player_eat(player_t *p, int id) {
    int h = id == B_SUGAR ? 1 : id == B_BUTTER ? 2 : id == B_FLORET ? 3 : 0;
    if (!h || id >= B_COUNT || p->inv[id] == 0) return -1;
    if (p->hp >= P_HEARTS) return 0;
    p->inv[id]--;
    p->hp = p->hp + h > P_HEARTS ? P_HEARTS : p->hp + h;
    return h;
}

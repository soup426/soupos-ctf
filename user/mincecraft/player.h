/* player.h - the cook in the world (v0.60.194). */
#pragma once
#include "fix.h"
#include "ray.h"
#include "render.h"
#include "blocks.h"
#define P_HW  19661          /* half the box's width, 0.3  */
#define P_H   117965         /* its height, 1.8            */
#define P_EYE 104858         /* the eye above the feet, 1.6 */
typedef struct {
    fx x, y, z;              /* the feet: the box's bottom centre */
    fx vy;
    int yaw, pitch;          /* 1024ths of a turn; pitch -227..227 (80 degrees) */
    int ground;
    unsigned short inv[B_COUNT];
    int slot;                /* the hotbar's chosen slot, 0..8 */
    int bx, by, bz, bn;      /* the block being broken, and ticks so far */
} player_t;
typedef struct { int fwd, side, turn, tilt, jump, brk; } input_t;   /* -1, 0, 1; jump and brk 0/1 */
void player_spawn(player_t *p);
int  player_tick(player_t *p, const input_t *in, const ray_t *target);   /* the id it broke, or 0 */
int  player_place(player_t *p, const ray_t *target, int id);             /* 1 placed, 0 refused */
int  hotbar(const player_t *p, int ids[9]);                              /* how many kinds held */
void player_eye(const player_t *p, cam_t *c);

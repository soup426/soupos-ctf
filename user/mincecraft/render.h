/* render.h - the view (v0.60.193). */
#pragma once
#include "fix.h"
#define BODY_W 640
#define BODY_H 480
typedef struct { fx x, y, z; int yaw, pitch; } cam_t;   /* the eye; angles in 1024ths */
void cam_forward(const cam_t *c, fx *dx, fx *dy, fx *dz);
int  render_alloc(void);                                 /* 0, or -1 */
/* light: 0..256 (256 noon); sky: 0xRRGGBB; target: the block aimed at
 * (outlined), crack: 0..256, how far it is broken (v0.60.200) */
typedef struct ray ray_t;
typedef struct { int x, y, z; } lamp_t;          /* a tealight's cell (v0.60.216) */
#define LAMP_REACH 6                              /* blocks it lights */
/* lamps: up to 16 near the camera, their light added to the day's */
void render(const cam_t *c, unsigned *body, int light, unsigned sky, const ray_t *target, int crack,
            const lamp_t *lamps, int nlamps);
extern fx render_cloud_drift;   /* (v0.60.237) set before render(): the clouds' drift along x */
extern int render_clouds;       /* (v0.60.237) 0: a clear sky */
extern int render_tod;          /* (v0.60.244) set before render(): the time of day, for the sun and moon */

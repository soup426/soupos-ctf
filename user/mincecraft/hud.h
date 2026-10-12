/* hud.h - drawn over the view at full size (v0.60.195). */
#pragma once
#include "player.h"
void hud_draw(unsigned *body, const player_t *p, const ray_t *target, int tod);

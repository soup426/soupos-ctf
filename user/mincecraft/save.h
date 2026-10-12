/* save.h - the world file (v0.60.197). */
#pragma once
#include "player.h"
int save_world(const char *path, const player_t *p, unsigned seed, int tod, unsigned *bytes);  /* 0, or -1 */
int load_world(const char *path, player_t *p, unsigned *seed, int *tod);   /* 0, -1 none, -2 damaged */

#pragma once
#include <stdint.h>

/* PS/2 mouse on the i8042's auxiliary device (IRQ 12). Positions are text
 * cells, 80x25, so they line up with everything else drawn on screen. */

int  mouse_init(void);
int  mouse_present(void);

/* Current pointer cell and button mask (bit 0 left, 1 right, 2 middle). */
void mouse_get(int *x, int *y, int *buttons);

/* Packets accepted, and packets dropped because the stream had slipped. */
void mouse_stats(uint32_t *moves, uint32_t *resyncs);

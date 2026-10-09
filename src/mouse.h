#pragma once
#include <stdint.h>

/* PS/2 mouse on the i8042's auxiliary device (IRQ 12). Positions are text
 * cells, 80x25, so they line up with everything else drawn on screen. */

int  mouse_init(void);
int  mouse_present(void);

/* Current pointer cell and button mask (bit 0 left, 1 right, 2 middle). */
void mouse_get(int *x, int *y, int *buttons);

/* The pointer in pixels, one raw count each, clamped to the screen
 * (v0.60.140): what the desktop draws its pointer at. */
void mouse_get_px(int *x, int *y, int *buttons);

/* How many times the left button has gone down (counted in the IRQ), and
 * the ticks of the last two presses (v0.60.145): a click is the count
 * moving, a double click two presses close together. */
uint32_t mouse_presses(uint32_t *last, uint32_t *prev);

/* The wheel's notches since boot, down (toward the user) positive; 0
 * always without a wheel (v0.60.151). Take the difference between looks. */
int mouse_wheel(void);

/* Packets accepted, and packets dropped because the stream had slipped. */
void mouse_stats(uint32_t *moves, uint32_t *resyncs);

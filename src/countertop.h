#pragma once

/* countertop - the soupOS desktop (v0.60.141), the surface everything is
 * set out on. It takes the framebuffer as mode 13h programs do (fbcon
 * paused, then resumed and repainted) and gives it back when it ends.
 *
 * avail/getch read the keyboard of the shell that started it. 0 when it
 * ended on Esc; -1 with no framebuffer; -2 with no memory for its pages. */
int countertop_run(int (*avail)(void), int (*getch)(void));

#pragma once
#include <stdint.h>

/* countertop - the soupOS desktop (v0.60.141), the surface everything is
 * set out on. It takes the framebuffer as mode 13h programs do (fbcon
 * paused, then resumed and repainted) and gives it back when it ends.
 *
 * avail/getch read the keyboard of the shell that started it. 0 when it
 * ended on Esc; -1 with no framebuffer; -2 with no memory for its pages. */
int countertop_run(int (*avail)(void), int (*getch)(void), int uid);   /* uid: whose terminals (v0.60.143) */

/* Windows for ring-3 programs (v0.60.146), for the SYS_WIN_* calls. They
 * run on the calling program's task; the desktop's loop is the only thing
 * that draws, so these only fill a table it reads under a mutex. -1 when
 * the desktop is not up, the id is not the caller's, or the window has
 * gone (the desktop left). */
int countertop_win_open(uint32_t pid, int w, int h, const char *title);
int countertop_win_size(uint32_t pid, int id, int *w, int *h);
int countertop_win_put(uint32_t pid, int id, const uint32_t *px);   /* px: w*h, checked by the caller */
int countertop_win_event(uint32_t pid, int id, void *ev);           /* a uwinev_t: 1, 0 none, -1 */
int countertop_win_close(uint32_t pid, int id);

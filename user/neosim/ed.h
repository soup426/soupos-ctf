/* ed.h - neosim's editor state (v0.60.204), shared by main.c and screen.c. */
#pragma once
#include "text.h"
enum { M_NORMAL, M_INSERT, M_COMMAND, M_VISUAL, M_VLINE };   /* visual (v0.60.206) */
#define SCR_COLS 80
#define SCR_ROWS 30
#define TEXT_ROWS 28          /* rows 0..27 the text, 28 the status, 29 the command line */
#define NUM_W 4               /* "  1 ": the line number and a space */
typedef struct {
    text_t t;
    char   path[128];
    int    mode, cy, cx, want; /* the cursor; want: the column j and k aim for */
    int    top, left;          /* the first line and column shown */
    int    changed;
    char   cmd[80];            /* what is typed after : */
    int    cmdn;
    char   msg[80];            /* the command line's message, until the next key */
    unsigned seq, saved, idgen;   /* (v0.60.205) the text's state, the one last written, the next id */
    int    ins_dirty;          /* this visit to insert mode changed something */
    int    vy, vx;             /* (v0.60.206) where visual mode was started */
    char   cmdch;              /* the command line's ':', '/' or '?' */
    int    help, htop;         /* (v0.60.219) the :help page shown, its first line shown */
} ed_t;
void screen_draw(const ed_t *e, unsigned *body);   /* 640x480 */

/* undo.h - neosim's undo and redo (v0.60.205): whole-text snapshots taken
 * before each change, in an arena; the oldest go when it fills. */
#pragma once
#include "text.h"
int  undo_init(void);                                         /* 0, or -1 */
void undo_push(const text_t *t, int cy, int cx, unsigned seq);   /* before a change; clears redo */
int  undo_undo(text_t *t, int *cy, int *cx, unsigned *seq);      /* 1 done, 0 nothing to undo */
int  undo_redo(text_t *t, int *cy, int *cx, unsigned *seq);      /* 1 done, 0 nothing to redo */
void undo_reset(void);                                        /* a new file */
void undo_discard(unsigned *seq);                             /* the last push came to nothing (an empty insert) */

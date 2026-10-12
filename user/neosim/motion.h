/* motion.h - neosim's word motions (v0.60.205), as vim's: three classes of
 * character (blank, keyword: letters digits _, anything else), and an empty
 * line counts as a word for w and b. */
#pragma once
#include "text.h"
void word_fwd(const text_t *t, int *y, int *x, int op);   /* w; op: as an operator's end */
void word_back(const text_t *t, int *y, int *x);          /* b */
void word_end(const text_t *t, int *y, int *x);           /* e */
int  first_nonblank(const text_t *t, int y);

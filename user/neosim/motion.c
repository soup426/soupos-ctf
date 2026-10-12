/* motion.c - neosim's word motions (v0.60.205). */
#include "motion.h"

static int cls(char c) {
    if (c == ' ' || c == '\t') return 0;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') return 1;
    return 2;
}
int first_nonblank(const text_t *t, int y) {
    int x = 0;
    while (x < t->len[y] && cls(t->s[y][x]) == 0) x++;
    return x < t->len[y] ? x : (t->len[y] ? t->len[y] - 1 : 0);
}
/* w: past the rest of this word, then past blanks, across lines; an empty
 * line stops it. At the end of the text it stays on the last character
 * (as an operator's end, just past it). */
void word_fwd(const text_t *t, int *y, int *x, int op) {
    int Y = *y, X = *x, L = t->len[Y];
    if (X < L) { int c = cls(t->s[Y][X]); if (c) while (X < L && cls(t->s[Y][X]) == c) X++; }
    for (;;) {
        while (X < L && cls(t->s[Y][X]) == 0) X++;
        if (X < L) break;
        if (Y + 1 >= t->n) { X = op ? L : (L ? L - 1 : 0); break; }
        Y++; X = 0; L = t->len[Y];
        if (L == 0) break;
    }
    *y = Y; *x = X;
}
/* b: back over blanks (an empty line stops it), then to the start of the
 * word there. */
void word_back(const text_t *t, int *y, int *x) {
    int Y = *y, X = *x;
    for (;;) {
        if (X > 0) X--;
        else {
            if (Y == 0) { *y = 0; *x = 0; return; }
            Y--; X = t->len[Y];
            if (X == 0) { *y = Y; *x = 0; return; }
            X--;
        }
        if (cls(t->s[Y][X]) != 0) break;
    }
    int c = cls(t->s[Y][X]);
    while (X > 0 && cls(t->s[Y][X - 1]) == c) X--;
    *y = Y; *x = X;
}
/* e: one on, past blanks and empty lines, then to the end of that word. */
void word_end(const text_t *t, int *y, int *x) {
    int Y = *y, X = *x;
    for (;;) {                                       /* one on */
        if (X + 1 < t->len[Y]) { X++; break; }
        if (Y + 1 >= t->n) { *y = Y; *x = X; return; }
        Y++; X = 0;
        if (t->len[Y]) break;
    }
    for (;;) {                                       /* past blanks */
        if (X < t->len[Y] && cls(t->s[Y][X]) != 0) break;
        if (X + 1 < t->len[Y]) { X++; continue; }
        if (Y + 1 >= t->n) { *y = Y; *x = X; return; }
        Y++; X = 0;
    }
    int c = cls(t->s[Y][X]);
    while (X + 1 < t->len[Y] && cls(t->s[Y][X + 1]) == c) X++;
    *y = Y; *x = X;
}

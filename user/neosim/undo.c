/* undo.c - neosim's undo and redo (v0.60.205). Two stacks of snapshots,
 * each a header (cursor, sequence number, line count) and the lines, each
 * a length byte and its characters, packed in its own arena. Undo pushes
 * the present onto redo and restores the top of undo; redo the reverse; a
 * new change empties redo. The sequence number lets the editor tell
 * whether the text is the one it last wrote. */
#include "../ulib.h"
#include "undo.h"

#define UNDO_ARENA (1024 * 1024)
#define REDO_ARENA (512 * 1024)
#define MAXSNAP 256

typedef struct { char *mem; int cap, top, n; int at[MAXSNAP]; } stack_t;
static stack_t U, R;

static int stack_init(stack_t *s, int cap) {
    void *p = sys_sbrk(cap);
    if (p == (void *)-1) return -1;
    s->mem = (char *)p; s->cap = cap; s->top = s->n = 0;
    return 0;
}
int undo_init(void) { return stack_init(&U, UNDO_ARENA) < 0 || stack_init(&R, REDO_ARENA) < 0 ? -1 : 0; }
void undo_reset(void) { U.top = U.n = 0; R.top = R.n = 0; }

static int size_of(const text_t *t) { int z = 16; for (int i = 0; i < t->n; i++) z += 1 + t->len[i]; return z; }
static void drop_oldest(stack_t *s) {                /* make room: the first snapshot goes */
    if (!s->n) return;
    int cut = s->n > 1 ? s->at[1] : s->top;
    for (int i = cut; i < s->top; i++) s->mem[i - cut] = s->mem[i];
    for (int i = 1; i < s->n; i++) s->at[i - 1] = s->at[i] - cut;
    s->top -= cut; s->n--;
}
static int push(stack_t *s, const text_t *t, int cy, int cx, unsigned seq) {
    int z = size_of(t);
    if (z > s->cap) return -1;                       /* bigger than the whole arena: no undo for it */
    while (s->n && (s->n == MAXSNAP || s->top + z > s->cap)) drop_oldest(s);
    char *p = s->mem + s->top;
    int h[4] = { cy, cx, (int)seq, t->n };
    for (int i = 0; i < 16; i++) p[i] = ((char *)h)[i];
    p += 16;
    for (int i = 0; i < t->n; i++) { *p++ = (char)t->len[i]; for (int k = 0; k < t->len[i]; k++) *p++ = t->s[i][k]; }
    s->at[s->n++] = s->top;
    s->top += z;
    return 0;
}
static void pop(stack_t *s, text_t *t, int *cy, int *cx, unsigned *seq) {
    char *p = s->mem + s->at[--s->n];
    int h[4];
    for (int i = 0; i < 16; i++) ((char *)h)[i] = p[i];
    p += 16;
    text_clear(t);
    for (int i = 0; i < h[3]; i++) {
        if (i) text_insert_line(t, i);
        int l = (unsigned char)*p++;
        for (int k = 0; k < l; k++) t->s[i][k] = *p++;
        t->len[i] = l;
    }
    *cy = h[0]; *cx = h[1]; *seq = (unsigned)h[2];
    s->top = s->at[s->n];
}
void undo_push(const text_t *t, int cy, int cx, unsigned seq) { push(&U, t, cy, cx, seq); R.top = R.n = 0; }
int undo_undo(text_t *t, int *cy, int *cx, unsigned *seq) {
    if (!U.n) return 0;
    push(&R, t, *cy, *cx, *seq);
    pop(&U, t, cy, cx, seq);
    return 1;
}
int undo_redo(text_t *t, int *cy, int *cx, unsigned *seq) {
    if (!R.n) return 0;
    push(&U, t, *cy, *cx, *seq);
    pop(&R, t, cy, cx, seq);
    return 1;
}
void undo_discard(unsigned *seq) {                   /* drop the last snapshot, its state is the present one */
    if (!U.n) return;
    char *p = U.mem + U.at[U.n - 1];
    int h[4];
    for (int i = 0; i < 16; i++) ((char *)h)[i] = p[i];
    *seq = (unsigned)h[2];
    U.top = U.at[--U.n];
}

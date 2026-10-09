#pragma once
#include <stdint.h>

/* The clipboard: one per cook, shared by everything that edits text.
 *
 * There is no mouse driver and no window system, so selection is keyboard
 * driven: `jot` marks a span and copies or cuts it, the shell's line editor
 * fills it when you kill text with Ctrl+U or Ctrl+W, and either one can paste
 * it back. Cutting in the editor and pasting at the prompt works because both
 * sides are talking to this, not to each other. */

#define CLIP_MAX 4096

/* Replace the contents. Anything past CLIP_MAX is dropped. */
void     clip_set(const char *data, uint32_t len);

/* Copy out at most `max` bytes; returns how many were copied. */
uint32_t clip_get(char *out, uint32_t max);

/* Bytes currently held, and a direct read-only view of them (not
 * NUL-terminated: use clip_len). */
uint32_t    clip_len(void);
const char *clip_peek(void);

void     clip_clear(void);              /* the current cook's */

/* Wipe a cook's clipboard: when they are fired, so the next cook hired
 * into the same uid does not inherit it. */
void     clip_forget(uint8_t uid);

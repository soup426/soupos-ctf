#pragma once
#include <stdint.h>

/* challenge.{c,h} — CTF content for the four-stage chain.
 *
 * Everything the challenge needs lives here so the whole thing can be built
 * out with CHALLENGE=0 and so the flags are in one auditable place. See
 * docs/challenge-chain-plan.md for the design and why each stage yields a
 * strictly stronger primitive than the last.
 *
 * Stage 1  Mise en Place   read a file the permission bits forbid
 * Stage 2  Salt to Taste   become uid 0
 * Stage 3  Bad Recipe      read kernel memory
 * Stage 4  Too Many Cooks  write kernel memory / hijack control flow
 */

/* Called from kernel_main after the filesystem and users are up. Places the
 * stage 1 flag on disk owned by headchef, and pins the stage 3 flag in the
 * kernel heap. */
void challenge_init(void);

/* Stage 2: prints its flag only when the caller is uid 0. Wired to the
 * `special` shell command. */
void challenge_special(void);

/* Stage 4: prints its flag. Nothing in the tree calls this — reaching it is
 * the challenge. */
void serve_the_special(void);

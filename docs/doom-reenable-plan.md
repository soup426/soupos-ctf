# Bringing Doom back

> **Complete 2026-08-26.** All seven steps are done.
> - Steps 1-2: port re-enabled, refuses to start with other tasks alive (v0.7.5)
> - Step 3: enemy AI fixed (v0.7.6) - the recorded diagnosis was wrong; the
>   real causes were a too-small wake radius and no line-of-sight test
> - Step 4: both bugs were already fixed by the 2026-08-25 vga13h work
> - Step 5: no heap leak. 56 bytes used before Doom, 20280 after one level,
>   20280 after two. Retained cache, not growth, against an 8 MB heap
> - Step 6: runs in a browser over noVNC
> - Step 7: `DOOM=0` excludes it from the build for the CTF image

Plan to un-park the Doom port (`doom-disabled/`, parked 2026-05-20 at v0.5.0)
against the current tree (v0.7.4). Written 2026-08-26.

## The good news: it still fits

I trial-compiled and link-checked the parked port against the current tree
before writing any of this. Results, all verified rather than assumed:

- **`doom.c` and `wad.c` compile with zero errors and zero warnings** under the
  current `CFLAGS`, including `-mgeneral-regs-only`.
- **No float, anywhere.** The port is pure fixed-point integer math, so the
  SSE ban that broke the build on GCC 16 does not touch it. This was the risk I
  most expected to bite, and it does not.
- **Zero unresolved symbols.** Everything `doom.o`/`wad.o` reference is either
  defined in the kernel, defined in `wad.c` itself, or `__divdi3` from libgcc,
  which the Makefile already links.
- **No filesystem API drift.** Doom uses only the `fat_f*` streaming calls
  (`fopen`/`fread`/`fseek`/`fclose`), which are unchanged. v0.6.0's move to
  absolute paths does not break it either: `path_split()` walks from the root,
  so the bare `DOOM1.WAD` in `wad.c` still resolves.
- **Input API intact.** `keyboard_key_pressed`, `keyboard_getchar` and
  `keyboard_flush` all still exist with the same shapes.
- **`DOOM1.WAD` already ships** on the disk image (4 MB, copied by `make disk`).

Four minor versions of kernel churn and the port still drops in clean. The work
below is entirely runtime behaviour, not porting.

---

## Step 1: mechanical re-enable

Straight from `doom-disabled/README.txt`, and still accurate:

- [ ] `git mv doom-disabled/{doom.c,doom.h,wad.c,wad.h} src/`
- [ ] `shell.c`: re-add `#include "doom.h"`, `cmd_doom()`, the `doom` dispatch
      branch, and `"doom"` in `cmd_names[]`
- [ ] No Makefile change: it globs `src/*.c`
- [ ] Add `doom` to the `menu` listing (it is paged now, so it will just work)

Expect this to build first try.

---

## Step 2: preemption, and one trap to avoid

**This is the significant change since the port was parked, and it needs care.**

Doom contains **zero** `task_yield()` calls in its gameplay path. It was written
against the cooperative scheduler, where monopolising the CPU was the correct
and intended behaviour. v0.7.4 armed timer preemption, and only `heap.c` was
ever made preemption-safe.

So with Doom running, IRQ0 can now schedule a background task mid-frame. Two
concrete failures follow:

- A background task (`bgclock` is the obvious one) writes to the VGA text
  buffer while Doom owns mode 13h and the DAC. Visible corruption.
- `fat.c` carries 44 unguarded file-scope statics. Doom streams lumps from the
  WAD during play; anything else touching the filesystem at the same time can
  corrupt shared FAT state.

### The trap

The obvious fix is `preempt_disable()` around the Doom session. **Do not do
that naively.** `sched_locked` is a single global shared with the scheduler,
and it unbalances across a context switch: whoever is switched *to* runs the
matching decrement. A `preempt_disable` region that yields therefore leaks its
critical section into an unrelated task.

And Doom *does* yield, just not where you would look. Gameplay uses the
non-blocking `keyboard_key_pressed()`, but the menus and the death screen call
`keyboard_getchar()`, whose wait loop calls `task_yield()`. Wrapping the whole
session in `preempt_disable()` would leak the lock the first time a player sat
on a menu.

### Options, in order of preference

1. **Refuse to start while other tasks exist.** If `task_count() > 1`, print
   "stop your background tasks first (`ps`, `kill <id>`)" and return. Trivial,
   zero risk, honest about the constraint. Good enough for a port whose point
   is that it is funny.
2. **Make `sched_locked` per-task**, then `preempt_disable()` around the render
   loop only (never around anything that calls `keyboard_getchar`). This is on
   the v0.7.4 debt list anyway and is the real fix.
3. **Make the drivers preemption-safe.** Correct, and much more work than this
   port deserves on its own.

Start with option 1. Revisit if Doom ever needs to coexist with a background
task, which it currently does not.

---

## Step 3: the parked enemy AI bug

`ROADMAP.txt` documents this in detail and it is the one known-broken feature.
Symptoms: enemies never move or attack, shots do not kill, ammo decrements
anyway, sprites stay on the idle frame.

Leading hypothesis in the roadmap: `lv_thing_ai`'s `kmalloc` returns NULL from
heap exhaustion, and `fire_weapon()` decrements ammo *before* its NULL check,
which is exactly the "ammo drops but nothing happens" symptom.

Diagnosis is much easier now than it was in May, because the OS grew the tools
in the meantime:

- [ ] `ladle` reports heap used/free. Capture it before and after `level_load`.
- [ ] `dmesg` gives a kernel log ring; log the `kmalloc` result instead of
      guessing.
- [ ] The panic handler dumps registers, CR2 and a stack walk if it faults.

- [ ] Confirm or kill the NULL hypothesis first. If confirmed, reduce
      `MAX_SPRITES` (128) or `MAX_FLAT_CACHE` (32) and re-measure.
- [ ] If `lv_thing_ai` is non-NULL, chase the roadmap's second hypothesis:
      `ENEMY_SEE_D2` is `512*512` and `dx*dx + dy*dy` can overflow int32 for
      distant things. Cast to int64 before multiplying. `CLAUDE.md` pitfall 6
      records a prior bug of exactly this shape.
- [ ] Fix `fire_weapon()` to check before it decrements regardless. That
      ordering is wrong even once the allocation succeeds.

---

## Step 4: things that may already be fixed

Two documented Doom bugs plausibly went away on their own, because `vga13h.c`
changed underneath them this week. **Verify, do not assume.**

- **"Death screen shows garbled rainbow pixels."** The roadmap's cause is
  Doom's PLAYPAL still sitting in the VGA DAC on return to text mode, and its
  suggested fix is resetting DAC entries 0-15 by hand. But `restore_regs()`
  already saves all 768 DAC bytes on mode entry and restores them on exit, so
  the text palette should come back on its own.
- **Garbled text after exiting.** `vga13h` now saves the 8 KB text font from
  plane 2 on entry and restores it on exit (added 2026-08-25, when mode 13h was
  found to be destroying the character generator). Any "text looks wrong after
  Doom" report predating that is likely already resolved.

`vga13h_enter()` also now clears the framebuffer, so Doom starts from a defined
screen instead of inheriting stale pixels.

- [ ] Play to a death screen and confirm the palette returns.
- [ ] Exit Doom and confirm the shell renders normally.

---

## Step 5: heap headroom

- [ ] Measure. The arena is a fixed 8 MB (`heap.c`, and the comment there
      literally says "enough for Doom's zone allocator"). The kernel has grown
      since May (console, editor, users, vfs, usermode, task), but almost all of
      that is `.text`/`.bss`, not heap, so pressure should be close to what it
      was. Confirm with `ladle` rather than trusting that reasoning.
- [ ] Note that `kfree` only forward-coalesces, so a long session fragments.
      Known debt; relevant if Doom loads several levels in a row.

---

## Step 6: playing it in the browser

The challenge instance renders the VGA framebuffer over noVNC, and mode 13h is
just a framebuffer, so Doom should appear with no extra work. VNC also delivers
key events to the PS/2 driver, so input needs nothing either.

- [ ] Try it. The open question is frame rate: noVNC sends dirty rectangles, and
      a 320x200 screen where most pixels change every frame is close to the
      worst case for that encoding.
- [ ] If it is too slow to play but fine to look at, that is still a good demo.

---

## Step 7: keep it out of the CTF image

The challenge build should **not** ship Doom.

- 3000 lines parsing a 4 MB data file is a large amount of extra attack surface
  in an image whose whole point is a precisely specified solve path.
- The WAD is 4 MB of the 32 MB disk.
- An unintended crash in the WAD parser is an unintended solve, or at least an
  unintended denial of service on a per-team instance.

- [ ] Gate it: a `DOOM=1` make variable, or a separate image flavour, so the
      CTF build and the fun build diverge cleanly.

---

## Suggested order

1. **Step 1**, mechanical re-enable. Should build immediately.
2. **Step 2 option 1**, refuse to start with background tasks. One `if`, and it
   removes the whole class of preemption corruption before you go looking for
   rendering bugs that are not there.
3. **Step 4**, check whether the palette and font bugs are already gone. Free
   information, and it changes what you are chasing.
4. **Step 3**, the enemy AI bug. The real work.
5. **Steps 5-7**, headroom, browser, build gating.

Doing step 2 before step 3 matters. Hunting a gameplay bug while a background
task is quietly corrupting VGA and FAT state is a bad time.

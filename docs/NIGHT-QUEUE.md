# Night queue

Work that can be done unattended, in order. The previous queue (nine items,
all completed on 2026-10-07) is in `docs/queue-archive-2026-10-07.md`; its
notes section is worth reading before starting here, because several of its
lessons are about how to test this machine rather than about its code.

## Standing rules

1. **One item, one commit.** If an item splits naturally, split the commits.
2. **`./scripts/smoke-test.sh` must pass before every commit**, and the
   configuration check must *gate* the commit, not merely precede it. A `;`
   where an `&&` belonged once let a broken `DOOM=0` through.
   The five configurations: default, `FB=0`, `DOOM=0`, `CHALLENGE=1 DOOM=0`,
   `FB=0 DOOM=0`.
3. **The other test scripts cover what the gate cannot**: `framebuffer-test.sh`
   (glyphs reach the screen), `doom-fb-test.sh` (the scaler matches mode 13h),
   `mixer-test.sh` (voices actually sum), `fullness-test.sh` (a failed write
   returns its clusters). Run the ones an item could plausibly break.
4. **Verify before believing.** Four of the five entries on the old known-bugs
   list were stale or mis-described, and one named a fix that measured 47%
   slower than the code it replaced. Prove the bug, then fix it.
5. **Measure before optimising**, and measure the thing you care about rather
   than the thing that is easy to measure. Duration told me nothing about
   whether two sounds mixed; an envelope correlation told me everything.
6. **Kitchen names for new shell commands.** `larder`, `whistle`, `sizzle`,
   `skewer`, `sample` are the recent ones.
7. **Never push anywhere but the Gitea remote.** Not GitHub. Never touch the
   `cdctf-release` branch or the CTF release files.
8. If an item is blocked, badly scoped, or needs a decision that is not written
   down, leave a note at the bottom and move to the next item.

---

## 1. Doom's frame rate on the framebuffer — DONE (v0.14.1): measured, left alone

The scaler writes about 2.3 MB per frame (960x600x4) where mode 13h wrote
64 KB. Nobody has measured what that costs, and guessing is how the VGA
scroll "optimisation" ended up 47% slower.

- `make PROFILE=1` makes Doom report `[doomprof] fps=... copy=...` to the
  serial log every 50 frames, which is exactly the number needed.
- **The obstacle, found 2026-10-07:** I could not drive Doom past its title
  screen headlessly, so the render loop never ran 50 frames and the profiler
  never reported. Two `KEY:ret`s and twenty seconds were not enough. Solve
  that first - find what key the title screen actually waits for, or add a
  way to start a level directly - because every later measurement needs it.
- Only then decide. If the copy dominates: a 2x scale is 1 MB/frame, Doom's
  status bar is static so dirty-row tracking would skip a third of the
  screen, and the row blit could move 64 bits at a time. Pick with numbers.

## 2. A listening TCP socket, and then a tiny HTTP server

The stack only dials out. `tcp.c` has one connection slot and a state machine
that starts at SYN_SENT; a passive open needs LISTEN, SYN received, SYN-ACK,
and a second slot so the listener survives the connection.

- Then `serve <port>` (kitchen-appropriate: it is what a kitchen does) answering
  GET for files on the FAT volume.
- Gate it from the host side: QEMU `-netdev user,hostfwd=tcp::18080-:80` and a
  `curl` from the host, checking the body of a known file byte for byte.
- The trap this will hit is the one DHCP and TCP both hit: SLIRP answers fast
  enough that a reply can arrive before the send call returns, so arm the
  receive side before sending anything that expects an answer.

## 3. VFAT long filenames, read side at least

8.3 names are a real limitation now that scripts, WADs and test files matter.
`LONGSTR.SC` is already a name chosen to fit, not a name anyone wanted.

- Read support first: the LFN entries precede their 8.3 entry in reverse
  order, each carrying a sequence number and a checksum OF THE 8.3 NAME.
  Validating that checksum is the part that gets skipped and the reason stale
  LFN chains show up as garbage filenames.
- `fat_ls` and `fat_find` are the two places that matter. Writing LFNs is a
  separate item; do not start it in the same commit.

## 4. Doom's music: the D_* lumps through the mixer

The mixer has four voices and a ring; music is what it was really for.

- MUS is a small event language (note on/off, pitch bend, controller) over 16
  channels, in `D_*` lumps. A synth is needed: start with one square wave per
  channel and the MUS tempo, NOT an OPL2 emulation, which is a project of its
  own.
- The mixer's voice count is the constraint worth thinking about before
  writing code: 16 MUS channels into 4 voices needs either more voices or a
  dedicated music voice that sums its own channels before handing over a
  single stream. The second is probably right.

## 5. A console that uses the whole screen

80x25 glyphs on 1024x768 leaves most of the display as border. 8x16 glyphs
give 128x48, which is a much better machine to use.

- `vga.c` hard-codes 80 and 25 in a lot of places, and so do the editor, the
  background clock and `fbcon`. **Generalise the dimensions first, as its own
  commit, with the gate green** - then changing the framebuffer console's size
  is a one-line follow-up.
- This is the sprawl-prone item in this queue. The text-mode build must stay
  80x25 because that is what the hardware is.

## 6. Per-task CPU accounting

`ps` shows what each task is doing but not what it costs, and the next
performance question will want that.

- Sample the current task in the timer interrupt, keep a per-task tick count,
  and show a percentage over the last second. The services (`fbcon`, `mixer`)
  are the interesting ones: nobody has checked what the console renderer
  actually costs, and it repaints thirty times a second.
- `sample` should assert something about it - an idle machine must not show the
  console renderer burning a large share.

## 7. A warnings sweep, then -Werror

`src/shell.c` has had a `variable 'sink' set but not used` warning all night,
which is exactly how warnings accumulate: one harmless one teaches you to read
past the rest.

- Fix every warning in a default build and under all five configurations, then
  add `-Werror` so the next one cannot be ignored.
- Expect `-Werror` to be the easy part and the fixes to turn up at least one
  thing that is actually a bug.

## 8. Volume, for the mixer

`whistle` and `sizzle` play at full scale, which is why two of them clip.

- A per-voice gain in the mixer (a shift or a 0-255 multiply) plus a master
  level, exposed as a command. The AC97 mixer registers already take an
  attenuation; software gain is the one that composes with mixing.
- Verify with the capture, not the ear: half gain should halve the measured
  amplitude, and the clipping harmonic from two overlapping tones should
  disappear.

---

## Notes left by unattended runs

Append here rather than editing the items above: what was finished, what was
stopped on and why, and anything that needs a human decision.

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

## 2. A listening TCP socket, and an HTTP server — DONE (v0.15.0)

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

**2026-10-07: done, v0.15.0.** `hatch [port]` serves files; scripts/hatch-test.sh
fetches them with curl from the host. The trap was not the one predicted: it was
ARP, not SLIRP timing. Replies are built in interrupt context where ip_send
cannot resolve, and SLIRP never ARPs us because it learns our MAC from DHCP, so
net_rx now learns the sender's MAC from every IP frame. One connection at a
time, by design - the listener is the connection - so a backlog is still open if
anyone wants it.

## 3. VFAT long filenames — DONE (v0.16.0, read side)

8.3 names are a real limitation now that scripts, WADs and test files matter.
`LONGSTR.SC` is already a name chosen to fit, not a name anyone wanted.

- Read support first: the LFN entries precede their 8.3 entry in reverse
  order, each carrying a sequence number and a checksum OF THE 8.3 NAME.
  Validating that checksum is the part that gets skipped and the reason stale
  LFN chains show up as garbage filenames.
- `fat_ls` and `fat_find` are the two places that matter. Writing LFNs is a
  separate item; do not start it in the same commit.

**2026-10-07: done, v0.16.0, gate 70.** Listing and opening both work, by long
name or by 8.3 alias, with the checksum validated. The predicted trap (skipping
the checksum) was handled; the ACTUAL trap was a 13-byte path-component buffer,
in two files - next_component in fat.c and normalize_path in shell.c - so the
name was truncated before the directory parser was ever reached. Writing long
names remains open and is still worth its own item.

## 4. Doom's music — DONE (v0.17.0)

The mixer has four voices and a ring; music is what it was really for.

- MUS is a small event language (note on/off, pitch bend, controller) over 16
  channels, in `D_*` lumps. A synth is needed: start with one square wave per
  channel and the MUS tempo, NOT an OPL2 emulation, which is a project of its
  own.
- The mixer's voice count is the constraint worth thinking about before
  writing code: 16 MUS channels into 4 voices needs either more voices or a
  dedicated music voice that sums its own channels before handing over a
  single stream. The second is probably right.

**2026-10-07: done, v0.17.0.** The second option was right: music is not a
voice at all, the mixer just adds one sample per frame from music.c, so all four
voices stay free. Square waves; instruments, pitch bend and percussion ignored
and documented. Verified against the score rather than by ear - the notes the
score says should sound in the first second are all in the capture - and the
envelope proves effects play over music without stopping it. Two self-inflicted
breaks: DOOM=0 needed music.c in its filter (as doomsnd.c did), and the gate's
audio timing had to move to its own script because QEMU writes only while the
card is active.

## 5. A console that uses the whole screen — DONE (v0.18.0)

80x25 glyphs on 1024x768 leaves most of the display as border. 8x16 glyphs
give 128x48, which is a much better machine to use.

- `vga.c` hard-codes 80 and 25 in a lot of places, and so do the editor, the
  background clock and `fbcon`. **Generalise the dimensions first, as its own
  commit, with the gate green** - then changing the framebuffer console's size
  is a one-line follow-up.
- This is the sprawl-prone item in this queue. The text-mode build must stay
  80x25 because that is what the hardware is.

**2026-10-07: done, v0.18.0.** The sprawl did not happen: generalising the
dimensions first (v0.18.0's parent commit) was five files' worth of constants
and changed no behaviour, after which the resize was setting the geometry in
one place. The one real subtlety: it must be set BEFORE any output, because the
cell store's row stride is the column count. The text build stays 80x25.

## 6. Per-task CPU accounting — DONE (v0.18.1)

`ps` shows what each task is doing but not what it costs, and the next
performance question will want that.

- Sample the current task in the timer interrupt, keep a per-task tick count,
  and show a percentage over the last second. The services (`fbcon`, `mixer`)
  are the interesting ones: nobody has checked what the console renderer
  actually costs, and it repaints thirty times a second.
- `sample` should assert something about it - an idle machine must not show the
  console renderer burning a large share.

**2026-10-07: done, v0.18.1.** The renderer costs 0% idle, which answers the
question the item was built around. The figure measures who HELD the cpu rather
than who worked - a task halted at a prompt still counts, so the shell reads
100% - which is stated in ps's own comment and in CLAUDE.md, because it is the
sort of number that invites a wrong reading.

## 7. A warnings sweep, then -Werror — DONE (v0.18.2)

`src/shell.c` has had a `variable 'sink' set but not used` warning all night,
which is exactly how warnings accumulate: one harmless one teaches you to read
past the rest.

- Fix every warning in a default build and under all five configurations, then
  add `-Werror` so the next one cannot be ignored.
- Expect `-Werror` to be the easy part and the fixes to turn up at least one
  thing that is actually a bug.

**2026-10-07: done, v0.18.2.** The prediction half held: the only -Wall warning
was the deliberate cpu-burner's accumulator, but adding -Wshadow immediately
caught a global added the same day shadowing vga13h_blit's parameter. Not a bug
yet, which is the point of catching it. Five configurations build clean.

### New item: a flaky job-control check

One gate run failed on "stopped job kept running: 1 lines while parked" and the
next passed, with nothing in that commit touching job control. The check asserts
a Ctrl+Z'd process prints nothing while parked; one line escaping looks like a
race between the stop taking effect and the process's next write. Find out
whether the kernel is late taking the stop at a safe point, or whether the
check is simply tighter than the stop can be - and do NOT loosen it until that
is known.

## 8. Volume, for the mixer — DONE (v0.19.0)

`whistle` and `sizzle` play at full scale, which is why two of them clip.

- A per-voice gain in the mixer (a shift or a 0-255 multiply) plus a master
  level, exposed as a command. The AC97 mixer registers already take an
  attenuation; software gain is the one that composes with mixing.
- Verify with the capture, not the ear: half gain should halve the measured
  amplitude, and the clipping harmonic from two overlapping tones should
  disappear.

**2026-10-07: done, v0.19.0. THE QUEUE IS EMPTY.** Both properties measured:
32767 -> 16383 at half, and 12 clipped samples -> 0 at 40. The interesting part
was the mixer test failing afterwards for an unrelated reason - the second
voice starts up to 63 ms late, which is the ring's three-chunk lead - and
fixing the test by understanding that rather than by lowering its threshold.

---

## Notes left by unattended runs

Append here rather than editing the items above: what was finished, what was
stopped on and why, and anything that needs a human decision.

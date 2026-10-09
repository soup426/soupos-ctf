# Night queue

Fifty-second queue. The first fifty-one are in `docs/queue-archive-2026-10-0*.md`;
queue 51 (`-09q`) made windows maximise, terminals resize, files drop onto
terminals, added oven.elf (and SYS_TONE) and made Leave ask first. This
one: terminal scrollback, snapping windows to halves, a window that lists
and ends processes, settings that persist, and notes from programs on the
bar. Written by an unattended run when queue 51 ran out, as the loop it
runs in asks; the user has not seen it.

## Standing rules

1. **One item, one commit**, and split when an item splits naturally.
2. **`scripts/check.sh` must pass before every commit** (five configurations
   in parallel copies, the gate's eight segments, every test script), run
   with `scripts/check.sh` here (the user allowed the laptop again later on
   2026-10-09; it runs at most CHECK_JOBS at once) or on the test container
   with `scripts/remote-check.sh` if the laptop is wanted for other things.
   Keep soaks six wide at most. Commit with
   explicit paths when other work is in the tree.
3. **Measure before building, and measure the fix.** An item whose premise
   measures false is closed with the numbers, not built anyway.
4. **Prove a bug before fixing it**, with the smallest reproduction that
   shows it, and turn that reproduction into the test.
5. **A test script per subsystem; the gate asserts on serial markers.** New
   scripts go in check.sh's list; a script waits on the PIDs it started,
   never a bare `wait` (QEMU is a child too).
6. **Reproduce under load**: six-wide soaks for anything timing-shaped.
7. **Crypto gets published vectors from an independent implementation.**
8. **Never pipe into `grep -q` in a test**: pipefail turns the writer's
   SIGPIPE into a failed check. lint-test.sh enforces it.
9. **Kitchen names** for every new command, builtins and programs alike
   (sift.elf, not grep.elf); compare with the host tool under its real name.
10. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
11. **`make clean` leaves disk.img alone**: `rm disk.img && make disk` after
    changing anything the Makefile embeds.
12. Do not edit the tree while check.sh runs: it builds once at its start
    and reads each test script when that test starts.
13. **Guard every new file**: `[ -e scripts/X-test.sh ] && { echo EXISTS;
    exit 1; }` before writing it (v0.60.128 wrote over headtail-test.sh
    after an `ls` printed that it was there).
14. **Check it is not already there**: grep the menu for "(sh's NAME)"
    and read the first lines of user/*.c before queuing an sh feature or
    a Unix tool (queue 42 queued wait and getopts, already rest and pluck).
15. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. Scrollback in terminal windows

A terminal window keeps the lines that scroll off its top (a few hundred,
per window) and the wheel over it scrolls back through them; typing, or
new output, puts it back at the bottom. Test: fill a terminal past its
height, wheel up, read the earlier lines back against the font, type a
key, the bottom again.

## 2. Snap to halves

A window dragged by its title bar until the pointer reaches the left or
right edge of the screen fills that half (between the bars), as maximise
does; dragged off again it goes back to its own size. Test by
screendump: snapped left, snapped right, unsnapped.

## 3. expo.elf: the processes, in a window

SYS_PROCS (a list of the processes: pid, name, cook, state, pages) and
expo.elf showing it as rows, refreshed every second, with a click on a
row and k to end that process; only the cook's own processes, or any for
the headchef, as `kill` already decides (the kernel checks, not expo).
Named for the expediter, who runs the pass. Test: glutton.elf running,
expo lists it, k ends it; as saucier, the headchef's process is refused.

## 4. Settings that stay

The desktop's background (a choice of a few gradients, from a Kitchen
menu entry or a small window) saved in the cook's home (.countertop)
and read back the next time countertop starts. Test: choose one, Leave,
countertop again, the background is the chosen one (screendump), and
another cook's desktop is not.

## 5. Notes from programs on the bar

SYS_NOTE: a program puts a short line on the bar for a few seconds
(queued if another is showing); oven.elf says "oven: done" there too,
which matters when its window is minimised. Test: oven minimised, the
note on the bar at zero (read against the font).

## 6. Doom at the framebuffer's own resolution — DEFERRED

Skipped at the user's request (queue 48): OS work only.

## Notes left by unattended runs

- From queue 48 (v0.60.143), still open: a terminal window shows stderr,
  as SSH sessions do, so peek.elf's "LS f SIZE NAME" serial markers
  (written for ls-test) appear under its listing there, and frost's
  [frost] lines too. Cosmetic; worth deciding whether such markers
  should go to the serial log only.
- From queue 48 (v0.60.145): the gate's boot segment failed once in
  three check.sh runs under load ("could not drive the QEMU monitor");
  if it comes back, look at qemu_keys' connect and login waits first.
- v0.60.149: cmp-test failed once under check.sh's load: it typed each
  line then waited a fixed 2 s, the guest fell behind after four, and
  QEMU was killed with four lines unread. Now it waits for each prompt.
  find, cut, glob, flip, quote, uniq, wc and tr tests have the same
  fixed wait (grep '"WAIT:2"); done'); item 8 should take them too.
- v0.60.150: the gate failed again only inside check.sh, this time its
  audio segment (no "[doomsnd] DSPISTOL" or "[music] D_E1M1" line, 98
  checks run, not 107); the gate alone passed (107 ok) and so did the
  rerun. With the boot segment's failure (v0.60.145) that is two of
  about ten check.sh runs today. Worth an item: run check.sh several
  times, count, and give the gate's segments what they wait for rather
  than fixed time under load.

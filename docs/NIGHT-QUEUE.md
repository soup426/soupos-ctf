# Night queue

Fifty-first queue. The first fifty are in `docs/queue-archive-2026-10-0*.md`;
queue 50 (`-09p`) cut the check's wall time (longest first, 48 jobs on the
CT), gave programs the wheel, put a Kitchen menu on the bar, ran jot over
SSH and added copy and paste to terminal windows. This one: windows that
change size, dragging between windows, a third program, and a guard on
Leave. Written by an unattended run when queue 50 ran out, as the loop it
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

## 1. Maximise and restore — DONE v0.60.161

A double click on a title bar fills the space between the bars with that
window, and another puts it back where and how big it was. Terminal
windows keep their 80x25 cells for now (centred in the space); files
and program windows keep their bodies too, so this item is the frame and
the bookkeeping. Test by screendump: maximised, restored, both logged.

## 2. Terminals that change size — DONE v0.60.162

Drag a terminal window's bottom-right corner to resize it, in whole
cells (40x10 at least, as many as fit at most): the wterm's cells and
rows/cols change, the shell (and jot) see the new size, and what was on
screen is kept as far as it fits. Measure first what the shell does with
cols() today when the line is longer than the width. Test: resize, run
something that fills a line, screendump against the font.

## 3. Drag a file onto a terminal

Dragging a row of the files window onto a terminal window types that
file's path there, quoted as files_open quotes it. Test: drag HELLO.TXT
onto a terminal, then `pour` and Enter; the terminal shows the file.

## 4. oven.elf: a kitchen timer in a window

A small window counting down a time given as an argument (`cook oven.elf
90` is a minute and a half), big digits drawn by the program, a beep
from the mixer at the end (if there is a sound device), and the window
going red. `eggtimer` the shell builtin already exists: this is the
windowed one, named for the oven. Test: a short time, screendumps of
the count and the end, the log of the beep.

## 5. Leave asks first when programs have windows

Leave with program windows open ends those programs with the desktop.
The first click on Leave with any open says how many (in the bar) and a
second within a few seconds leaves; none open, it leaves at once. Test:
frost open, Leave once (still up, the bar says so), Leave again (down).

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

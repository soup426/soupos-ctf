# Night queue

Forty-ninth queue: the desktop, carried on. The first forty-eight are in
`docs/queue-archive-2026-10-0*.md`; queue 48 (`-09n`) built countertop up
to windows for ring-3 programs (frost.elf), the files window and the
taskbar. This one closes the two holes those items found (kernel copies
from user memory nobody mapped; window calls nobody has attacked), then
the wheel, a second graphical program, keyboard switching, and the test
suite's typing time. Written by an unattended run when queue 48 ran out,
as the loop it runs in asks; the user has not seen it.

## Standing rules

1. **One item, one commit**, and split when an item splits naturally.
2. **`scripts/check.sh` must pass before every commit** (five configurations
   in parallel copies, the gate's eight segments, every test script), run
   on the test container with `scripts/remote-check.sh` (about six
   minutes), never on the laptop: the user asked (2026-10-09). Soaks and
   single tests may run here; keep soaks six wide at most. Commit with
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

## 1. A bad user pointer kills the caller, not the kernel — DONE v0.60.149

Read from the code in v0.60.146, not yet run: user_ok checks that a
pointer is inside the user range, nothing more, so a syscall copying from
an address the program never had (above its break, below its stack) would
fault in ring 0, and isr.c's demand-paging hook would decline it and
panic. Prove it first with a program that hands SYS_WRITE such a pointer
(the smallest reproduction), then make that fault kill the program the
way a ring-3 fault does, with the same "[user] fault" log line. Test:
the program is killed, the kernel lives, the next command runs.

Proven (all four of write, the image gap, read, stat panicked), then
built as -1 from the call rather than a kill: user_ok checks the pages
before the copy, because a kill from inside the fault could unwind a
call holding FAT's or the desktop's mutex.

## 2. prod.elf: the window calls, attacked — DONE v0.60.150

A program that does what frost would never: SYS_WIN_* with another
program's id (opened by a second copy of itself), sizes of 0 and 641,
a pixel pointer past the user range and one half past it, a title that
is not terminated, an event pointer into kernel memory, calls after the
desktop has gone. Each must give -1 and change nothing (screendump the
other program's window before and after). Kitchen name: prod.

## 3. The wheel — DONE v0.60.151

The PS/2 IntelliMouse handshake (sample rates 200, 100, 80; ID 3 means a
wheel, 4-byte packets), with the 3-byte mouse kept working when the ID
stays 0. QEMU's `mouse_move dx dy dz` turns it. The files window scrolls
by it. Measure first that QEMU's PS/2 mouse answers ID 3. Test: the
handshake's ID in the log, and a files window scrolled by dz (screendump).

## 4. How fast can a program draw? — MEASURED, CLOSED v0.60.152

Measure before building anything: a program that puts N frames of
640x480 and of 320x240 and reports ticks per frame, idle and with a
terminal busy. If a full-body put is too slow for something like 30
frames a second, add a put of a rectangle and measure again; if it is
fast enough, close the item with the numbers.

Measured with sear.elf: a 640x480 put costs 1.7 ms idle, 3.1 ms with a
CPU hog (300-550 a second); the desktop draws one in ~0.8 ms and shows
45-50 frames a second, held by its 15 ms nap. Fast enough: no rectangle
put. Numbers in ROADMAP v0.60.152.

## 5. swirl.elf: a Mandelbrot set in fixed point — DONE v0.60.153

A second program on the window calls: Mandelbrot in 16.16 fixed point
(no FPU in ring 3), drawn row by row so the window fills as it computes;
a click zooms in on that point, - zooms out, q quits. Test: the host
computes the same fixed-point image in Python and the screendump matches
it pixel for pixel; a click's zoom matches the host's too.

## 6. jot in a terminal window — DONE v0.60.154

Does the editor work in a wterm (cursor moves, full-screen redraw, its
keys)? Measure by running it there. If it does, the files window opens
text files (.txt .md .sc .sh) in jot and the rest in pour; if it does
not, fix what wterm lacks first, and test the edit by screendump and by
the file on disk afterwards.

## 7. Switching windows from the keyboard

Alt+Tab raises the next window down (keyboard_key_pressed gives Alt;
measure that Tab with Alt held arrives as '\t'). Minimised windows are
skipped. Test: three windows, Alt+Tab twice, the focus on the third
(screendump of the title bars and the taskbar's blue button).

## 8. The suite's typing time

seq-test went from 188 s to 7 s when its cases ran from a script on the
disk instead of typed lines (v0.60.148). Measure which tests in
check.sh's full run take longest and how much of that is typing; move
the slowest such tests to scripts and measure check.sh's wall time
before and after. A test whose point is the typing (line editing,
history, keys) stays typed.

## 9. Doom at the framebuffer's own resolution — DEFERRED

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

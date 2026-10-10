# Night queue

Fifty-third queue. The first fifty-two are in `docs/queue-archive-2026-10-0*.md`;
queue 52 (`-09r`) added scrollback, snapping to halves, expo.elf (with
SYS_PROCS and SYS_KILL), settings kept in the cook's home and notes on the
bar (SYS_NOTE). This one: keyboard shortcuts, changing files from the
files window, a calculator, icons on the background and a calendar under
the clock. Written by an unattended run when queue 52 ran out, as the
loop it runs in asks; the user has not seen it.

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

## 1. Keyboard shortcuts — DONE v0.60.171

Ctrl+Alt+T opens a terminal and Alt+F4 closes the focused window (a
program's gets WEV_CLOSE, as its close box sends). Measure first what the
keyboard delivers for F4 (there is no KEY_F4 yet) and whether Ctrl+Alt+T
arrives as Ctrl+T with Alt held. Test: both shortcuts, screendumps and
the log.

## 2. New bowls and removing files from the files window — DONE v0.60.172

A right click in a files window: a small menu with "new bowl" (named
bowl, bowl2, ... in the shown one) and, on a row, "remove" (asked twice,
as Leave is). The same permission rule as the shell's (users_may); a
bowl that is not empty is not removed. Test: as saucier in their home,
a bowl made and a file removed; in / refused.

## 3. scale.elf: a calculator — DONE v0.60.173

A window of buttons (digits, + - x /, =, C) clicked or typed, decimals in
fixed point (ring 3 has no FPU), the display drawn with the kernel's font.
Named for the kitchen scale. Test: 12.5 x 4 = 50, 7 / 2 = 3.5, 1 / 0 an
error, typed and clicked, the display read against the font.

## 4. Icons on the background

The Kitchen menu's programs as icons down the left of the background
(a drawn glyph and the name under it); a double click opens one as the
menu does. Windows cover them; a click on the background between them
does nothing. Test: double click swirl's icon, its window opens.

## 5. A calendar under the clock

A click on the clock drops down this month (from the RTC), today
marked; another click or a click away puts it up. Test: the month's
first row and today's mark read back against the font, with the date
the RTC gives QEMU.

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

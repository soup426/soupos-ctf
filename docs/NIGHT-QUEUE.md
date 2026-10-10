# Night queue

Fifty-sixth queue. The first fifty-five are in `docs/queue-archive-2026-10-0*.md`;
queue 55 (`-09u`) added closing the kitchen (power off and reheat from the
desktop), a menu on the background, Print Screen to a BMP, and pictures
from the files window. This one: the clock in the cook's own time, a
window for the desktop's settings instead of a file to edit, a compost
bowl so a removed file can come back, and Alt+Tab that shows where it
is going. Written by an unattended run when queue 55 ran out, as the
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

## 1. The clock in the cook's own time — DONE v0.60.187

The RTC is UTC (QEMU's default), so the bar says 02:00 at 9 pm in
Alabama. Two settings in the cook's `countertop`: "zone -5" (hours from
UTC, -12 to +14) and "clock 12" (12-hour, with am/pm). The bar's clock
and the calendar under it (today's date crosses midnight with the zone)
use them. Test: with zone and 12-hour set, the bar read back against the
host's UTC time shifted the same way; the calendar's today mark on the
shifted date.

## 2. The spice rack: the desktop's settings in a window

A window (Kitchen menu and the background menu, "spice rack") with the
settings as things to click: the four backdrops as swatches, the
picture (its path, and "none"), the cloche's idle minutes (- and +, and
never), the zone (- and +) and 12/24-hour. Each click takes effect at
once and is written to the cook's settings. Test: each control clicked,
the screen and the settings file after each.

## 3. Compost: removing a file can be undone

The files menu's remove moves a file into the cook's compost bowl
(`compost` in their home; the headchef's `/compost`), not off the disk;
a name already there gets a number. Opened, the compost's files menu
offers "put back" (to where it came from, kept in a small index file)
and "empty the compost" (asked twice, then really gone). Test: a file
removed and put back byte for byte; removed again and emptied, gone
from the disk.

## 4. Alt+Tab you can see

While Alt is held after a Tab, a panel in the middle of the screen
lists the windows (title, the focused one marked); each Tab moves the
mark, and letting go of Alt raises the marked window. The keyboard IRQ
already decides Alt combinations; Alt's release needs to reach the
desktop too. Test: three windows, Alt+Tab+Tab, the panel read back
with the third marked, Alt let go, the third on top.

## 5. Doom at the framebuffer's own resolution — DEFERRED

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

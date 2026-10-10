# Night queue

Fifty-fifth queue. The first fifty-four are in `docs/queue-archive-2026-10-0*.md`;
queue 54 (`-09t`) added the cloche (locking the desktop), copying files by
dragging, the user's two requests (programs without a terminal, booting
straight to the desktop's clock-in screen), CPU use in expo, the network
on the bar and a picture for the background. Since the desktop is now
where the machine starts, this one makes it able to stand on its own:
putting the machine away from it, a menu on the background, a picture of
the screen, and pictures from the files window. Written by an unattended
run when queue 54 ran out, as the loop it runs in asks; the user has not
seen it.

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

## 1. Closing the kitchen from the desktop — DONE v0.60.183 (menu only)

Booted straight to the desktop there is no prompt to type `freeze` or
`reheat` at. The clock-in screen and the Kitchen menu offer "close the
kitchen" (power off: ACPI's PM1a control port on QEMU's PIIX4, so QEMU
exits, falling back to freeze's halt) and "reheat" (reboot), the
headchef's only, as at the prompt: anyone else is told so. Leftovers
saved first, as freeze does. Measure first how QEMU's i440fx machine
takes the ACPI write. Test: as the headchef from the menu, QEMU's process
exits by itself; as saucier refused (logged); reheat reboots (the boot
banner twice in the serial log).

## 2. A menu on the background — DONE v0.60.184

A right-click on the background (not a window, icon or bar) drops a menu
where it was clicked: Terminal, Files, Kitchen (the programs as a
submenu or the menu itself), backdrop, cloche. A click away or Esc puts
it up. Test: the menu read back at the click, each entry doing what its
button does, a right-click on a window not opening it.

## 3. A picture of the screen — DONE v0.60.185

Print Screen saves the whole screen as a 24-bit BMP in the cook's home,
"plate1.bmp", "plate2.bmp" and on (the first name not taken), said on
the bar. Read back by the picture code, it can be the background. Test:
the file's pixels against the screendump QEMU takes at the same moment
(nothing moving), and named as the background, drawn back the same.

## 4. Pictures from the files window

A double click on a .bmp opens it in a window of its own (a platter:
the picture at its size, scrolled with the wheel if bigger than the
window); the files menu's right-click gains "use as background", which
writes the picture line into the cook's settings and redraws. Test: a
host-made BMP opened (pixels compared), then set as the background
(settings file and the screen).

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

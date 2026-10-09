# Night queue

Forty-eighth queue: a desktop for soupOS, at the user's request (2026-10-09:
"start working on getting a desktop for soupOS ... whatever you want to get
to that goal"). The first forty-seven are in `docs/queue-archive-2026-10-0*.md`.

What is there to build on: the default image boots GRUB's 1024x768x32 linear
framebuffer (fb.c); fbcon draws the text console onto it as 128x48 cells of
font8x16; a PS/2 mouse reports cells (4 counts each); mode 13h programs
pause fbcon and take the screen. The tests boot that image, inject keys and
mouse moves through QEMU's monitor, and can screendump the pixels
(framebuffer-test.sh compares glyphs against the font). The kernel heap is
8 MB; a 1024x768x4 back buffer is 3 MB, so where the desktop's memory comes
from is the first thing to measure.

The desktop is `countertop` (the surface everything is set out on). Each
item below leaves something that works and is tested by screendump.

## Standing rules

1. **One item, one commit**, and split when an item splits naturally.
2. **`scripts/check.sh` must pass before every commit** (five configurations
   in parallel copies, the gate's eight segments, every test script; about
   two minutes). Commit with explicit paths when other work is in the tree.
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

## 1. The pointer in pixels — DONE v0.60.140

mouse.c keeps text cells. Keep a pixel position too (the framebuffer's
1024x768, clamped), with mouse_get_px(), and the cells derived from it as
now so skewer, drag-select and the gate are unchanged. Test: injected moves
land on the pixel expected (screendump not needed; a shell command or the
serial log can say where).

## 2. countertop: the surface — DONE v0.60.141

`countertop` takes the framebuffer (as mode 13h programs do: fbcon paused,
then resumed): a desktop background, a bar along the top with "soupOS" and
the time (expiry's), and a pointer drawn where the mouse is, moving with it;
Esc gives the console back as it was. Drawn into a back buffer and copied
out only where something changed. Measure first where 3 MB comes from (the
heap, or pages of its own). Test by screendump: the bar, the background,
the pointer where it was moved to, and the console intact after Esc.

## 3. Windows — DONE v0.60.142

A window list in z-order: each with a title bar (its name, a close box), a
border and a body. Click to focus and raise, drag the title bar to move,
the close box to close. Two demo windows to start. Test by screendump after
injected clicks and drags: raised, moved, closed.

## 4. Terminal windows — DONE v0.60.143

A term_t whose cells are drawn into a window (font8x16), keyboard input to
the focused window, and a shell task of its own in each (as pass and vault
give a remote session one). Several at once, each its own cwd and history.
Test: open two, type a different command in each, screendump, compare the
glyphs against the font as framebuffer-test does.

## 5. A bar of its own: launcher and taskbar — DONE v0.60.144

Along the bottom: a button that opens a terminal, one per open window
(click to raise, or to bring back a minimised one), and the clock. A
minimise box on each window. Test by screendump after clicks.

## 6. A files window — DONE v0.60.145

A bowl's entries (fat_ls), bowls first; a double click enters a bowl (and ..
goes up), on a file opens a terminal window with it in pour (or jot for a
text file). Test by screendump and by what the opened terminal shows.

Built with pour for every file: jot in a terminal window is untested, and
a pour of a .elf prints its bytes (a "cook it" for .elf is a later choice).

## 7. Windows for ring-3 programs — DONE v0.60.146

Syscalls to make a window, put a buffer of pixels into it, and wait for its
events (keys, the mouse in its coordinates, close); a first program that
uses them (a paint program, `doodle.elf`, or a Mandelbrot viewer). Test:
cook it, draw with injected drags, screendump, close.

Built as frost.elf (doodle is not a kitchen name: icing a cake).

## 8. Later: layer -s and - (paste), tally with decimals (seq) — DONE v0.60.147, v0.60.148

Moved from queue 47 so the desktop comes first; as written there.

## 9. Doom at the framebuffer's own resolution — DEFERRED

Skipped at the user's request: OS work only.

## Notes left by unattended runs

- v0.60.143: a terminal window shows stderr, as SSH sessions do, so
  peek.elf's "LS f SIZE NAME" serial markers (written for ls-test) appear
  under its listing there. Cosmetic; worth deciding whether peek should
  write them only when stderr is the serial log.
- v0.60.145: the first check.sh run failed in the gate's boot segment
  only ("[boot] could not drive the QEMU monitor", and every check after
  it), with load near 22 and the whole run at 297 s, not ~180. The gate
  alone passed (107 ok), and so did check.sh again (176 s). One failure
  in three runs; if it comes back, look at qemu_keys' connect and login
  waits under load before anything else.
- v0.60.146: read from the code, not run: a syscall that copies from a
  user address the program never mapped (inside the user range, outside
  sbrk and the stack) would fault in the kernel, and isr.c would panic
  rather than kill the program, since user_ok checks the range only.
  Not new (SYS_WRITE and the rest share it); SYS_WIN_PUT copies up to 1.2 MB, so it is the
  easiest way to hit it. Worth an item: prove it with a program, then
  kill the caller instead.
- v0.60.146: no test has a second program try another's window id
  (countertop_win_* check the pid; nothing exercises it).
- v0.60.148: seq-test typed each case and waited for its prompt: 57
  cases took 188 s alone, 7 s once they ran from a script on the disk
  (. /SEQ.SH). The other compare-with-the-host tests are built the same
  way; check.sh's wall time is set by its slowest tests, so the same
  change there is worth measuring.

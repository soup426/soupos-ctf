# Night queue

Fiftieth queue. The first forty-nine are in `docs/queue-archive-2026-10-0*.md`;
queue 49 (`-09o`) closed the bad-pointer panic, attacked the window calls,
added the wheel, swirl.elf, jot in windows and Alt+Tab, measured drawing,
and cut the suite's typing. The full check now runs on CT 123 on atlas
(scripts/remote-check.sh), never on the laptop. This queue: the check's
own wall time, then the desktop carried on. Written by an unattended run
when queue 49 ran out, as the loop it runs in asks; the user has not
seen it.

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

## 1. The longest jobs first — DONE (check.sh, 2026-10-09)

check.sh starts its ~205 jobs in list order, so users-test, testcmd-test,
bowls-test and cooks-test (115-124 s under load) can start late and set
the wall time: 353 s against ~258 s of work at 32 wide. Keep each job's
time from the last run (on the machine that ran it) and start the
longest first. Measure check.sh's wall time before and after, twice each.

Measured on CT 123 at 32 jobs: 353 s and 352 s before; 356 s on the
first run after (no times yet, so it only records them); 297 s and 298 s
after that. lint-test now also fails when a test script is in no list:
rewrapping the list split 13 names at their hyphens and the old checks
passed without them.

## 2. How many jobs at once

At 64 jobs the check took 266 s but peaked at 7722 MB of CT 123's 8192.
Measure peak memory at 32 and 48 (a sampler in the CT, started so that
it does not kill itself), and set remote-check's default to what leaves
a gigabyte spare. Do not grow the CT without asking: atlas's other
guests share its memory.

## 3. The wheel for programs

A WEV_WHEEL event (notches, signed, and where the pointer was) for the
window under the pointer when it is a program's; swirl zooms with it.
Test: injected notches over swirl's window, the view compared with the
host's as swirl-test does.

## 4. A menu of programs on the bar

A "Kitchen" button on the top bar listing the programs that open
windows (frost, swirl), each opening in a new terminal as the files
window does with pour and jot. Test by screendump and the log.

## 5. jot over SSH

The stream terminal could implement put_cell with VT100 cursor
addressing (ESC[row;colH) and SGR colours; then jot would work over
SSH, where it is refused now. Measure first what a full redraw costs in
bytes; if it is too much, send only the cells that changed. Test with
the ssh tests' pexpect harness: open jot, type, save, read the file.

## 6. Copy and paste in terminal windows

The console has drag-select with the mouse (skewer's cells) into the
shared clipboard. In a terminal window: a drag selects cells (shown
inverted), the selection goes to the clipboard, and a middle or right
click pastes into the focused terminal. Test by screendump and by what
a pasted command does.

## 7. Doom at the framebuffer's own resolution — DEFERRED

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

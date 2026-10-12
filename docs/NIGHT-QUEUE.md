# Night queue

Fifty-eighth queue. The first fifty-seven are in `docs/queue-archive-2026-10-*.md`;
queue 57 (`-12a`) built neosim (normal and insert modes, operators, undo,
search, :s, visual, ., from the files window), Mincecraft's review leftovers
and sounds, check.sh under load, and Doom at 960x600. This one: Mincecraft
with the mouse, crafting and light; neosim in colour; typing after a login
in the tests. Written by an unattended run when queue 57 ran out, as the
user asked on 2026-10-11 ("if the queue is empty, add stuff to the queue");
the user has not seen it.

## Standing rules

1. **One item, one commit**, and split when an item splits naturally.
2. **`scripts/check.sh` must pass before every commit** (five configurations
   in parallel copies, the gate's eight segments, every test script), run
   here on the laptop: the user asked on 2026-10-11 for everything to run
   locally, so not `scripts/remote-check.sh`.
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
16. **Never run an interactive QEMU on the tree's disk.img** (2026-10-11: a
    session's /etc and /countertop writes failed eight tests). Copy it.
17. **Test directories are kept on failure, 50 MB each**: clear
    /tmp/soupos-* after a run of failures, and never let a test compute a
    size from parsed tool output without checking it is a sane number
    (2026-10-11: `head -c` of a misparsed negative count wrote 11 GB).

---

## 1. Mincecraft: look with the mouse — DONE v0.60.214

Arrows only, today: the desktop passes left-button presses and drags to a
program's window, and a right press in a body is not handed on
(countertop.c, the background-menu path takes btn & 2 only where there is
no window). Hand right-button presses and drags to programs too (WEV_DOWN /
MOVE / UP with buttons bit 1), then Mincecraft turns and tilts by the drag
(so many pixels a step). Test: a right-button drag across the body turns the
cook (the yaw in the -script markers) and a left one still breaks; frost
and the other window programs unchanged (their tests).

## 2. Mincecraft: crafting — DONE v0.60.215

The spec's first "later": c on the hotbar's broccoli stalk turns one into
four of a new block, toast (golden, crisp), and c on parmesan two into a
cheese grater block (grey, holes)... kept small: a recipe a key, said on
the bar, each a test (inventory before and after, in the markers).

## 3. Mincecraft: tealights — DONE v0.60.216

A placeable light (the night is dark): a block, tealight, crafted from
butter (one makes four), that lights the cells within 6 blocks by
distance, the light added to the day's. render() takes a light field
(rebuilt in the region when one is placed or broken). Test: a screendump at
midnight, a tealight placed ahead: the floor around it brighter than the
same floor without; the frame rate still at the cap (measure first).

## 4. neosim: colour — DONE v0.60.217

Syntax colours for .sc (soupyc: keywords, strings, numbers, comments) and
.c (the same, and #lines), chosen by the file's name, drawn by screen.c
from a per-line pass (no state across lines but a /* comment). Test: a
screendump of a small .c file, each class's cells in its colour.

## 5. The first line after a login

From queue 57's notes: litexp-test once lost its first line, typed a
second after the secret, while the login was being checked (an empty
output file, the rerun fine). Measure first: how many tests type a line
right after "rosemary" with only a fixed WAIT, and does a soak of one of
them, six wide, lose the line? If so, qemu_keys waits for the prompt after
a login's secret before the next item, for every test at once.

## Notes left by unattended runs


(Carried from queue 56, still open.)
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
- From queue 57 item 4 (v0.60.207): the double click on a text file still
  opens jot, as it has since v0.60.154; only the menu's "open in neosim" was
  added. Moving the double click to neosim is the user's call: one line in
  files_open (countertop.c), and ctfiles-test's double-click checks follow.
- v0.60.212: litexp-test failed once under check.sh ("expanded text differs
  from bash:" with nothing after it, so its output file was empty), passed
  three of three alone and the rerun passed. It already waits for each
  prompt; the empty file suggests the first line, typed one second after the
  secret, was lost while the login was checked. If it comes back, wait for
  the first prompt (UNTIL) before the first line, in every test that types
  straight after logging in.

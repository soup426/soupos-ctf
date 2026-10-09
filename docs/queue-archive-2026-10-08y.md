# Night queue

Thirty-second queue. The first thirty-one are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Scripts get shift, a command can be turned around with
!, a whole loop or if can go to a file, & separates commands, and
arithmetic learns to stop early and to choose.

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
13. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. shift [N] — DONE v0.60.59

Drops the first N (default 1) of a script's or function's arguments, so
$1 is what $2 was, and $# and $@ follow. Status 1 when there are fewer
than N. A special builtin in sh, so it keeps its name. Compare with bash.

## 2. ! COMMAND — DONE v0.60.60

`! cmd` runs cmd and turns its status round: 0 becomes 1, anything else 0.
Works on a pipeline, in if and while conditions, and with && and ||; and
follow -e does not stop on it, as bash -e does not. Compare with bash.

## 3. done > FILE, fi > FILE — DONE v0.60.61 (with < FILE; 2> refused, see ROADMAP)

A loop's or an if's whole output to a file (>, >>, and 2> with them),
which the shell refuses today. Compare the file with bash's.

## 4. & between commands — DONE v0.60.62

`a & b` runs a in the background and b at once, as `a & ; b` does today.
$! is a's. Compare with bash.

## 5. && || and ?: in arithmetic — DONE v0.60.63

$(( )) and (( )) evaluate both sides of && and || today; sh evaluates the
right only when it counts, which matters for assignments and ++. Add
c ? a : b, which evaluates one side only. Compare with bash.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

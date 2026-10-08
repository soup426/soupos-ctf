# Night queue

Twenty-first queue. The first twenty are in `docs/queue-archive-2026-10-0*.md`.
Two items wait on a person, with notes in their archives: the seventh's
syscall-pointer item and the thirteenth's swap-shares item. The twentieth
gave programs SYS_STAT and added ls -l and find; this one gives scripts
control flow, now that they have variables, test.elf and globs.

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
8. **Kitchen names** for new shell commands.
9. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
10. **`make clean` leaves disk.img alone**: `rm disk.img && make disk` after
    changing anything the Makefile embeds.
11. Do not edit the tree while check.sh runs: it builds once at its start
    and reads each test script when that test starts.
12. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. for loops — DONE v0.59.3

`for NAME in WORDS ; do COMMANDS ; done` on one line (and in a script): the
words expanded first (globs, $vars), NAME set to each in turn, COMMANDS run
through run_line each time; the loop's status is the last command's.
Compare what a loop leaves behind with sh running the same line.

## 2. if blocks — DONE v0.59.3

`if COMMANDS ; then COMMANDS ; [else COMMANDS ;] fi` on one line: the
branch by the exit status of the condition's last command, as sh does.
Nested inside for, and for inside if. Compare with sh.

## 3. cmp.elf — DONE v0.59.4

`cmp.elf F1 F2`: exit 0 when the same, 1 with "F1 F2 differ: byte N, line
M" when not, "cmp: EOF on F1" when one is a prefix of the other, 2 on a
missing file, as GNU cmp does. Compare messages and codes with the host.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

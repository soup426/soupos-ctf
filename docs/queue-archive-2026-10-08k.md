# Night queue

Eighteenth queue. The first seventeen are in `docs/queue-archive-2026-10-0*.md`.
Two items wait on a person, with notes in their archives: the seventh's
syscall-pointer item and the thirteenth's swap-shares item. The seventeenth
gave the shell quotes and ~ and made the tests hold up under load; this one
closes the gap quotes left in the builtins and lets a cook keep commands in
a file.

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

## 1. Builtins take quoted arguments — DONE v0.57.4

Programs have understood quotes since v0.57.2; builtins still take their
raw text, so `pour "my file.txt"` looks for a name with quotes in it.
Find every builtin that takes a path or text, give them the same splitting
(one shared helper), and test the ones that take paths on a file whose
name has a space.

## 2. run FILE — DONE v0.57.5

A builtin that runs a file of command lines as the cook who runs it: each
line through run_line, so ; && || $? ~ and quotes all work, '#' lines are
comments, and it stops on nothing. Its status is the last line's. Compare
what a small script leaves behind with sh running the same lines.

## 3. Bowls with long names — DONE v0.57.6

Found testing item 1: `mkbowl "/a bowl"` says "Bowl ready" and leaves a
directory entry named `A BOWL` (a space inside an 8.3 name, which 8.3 does
not allow); cd and rmbowl then cannot find it. fat_mkdir never writes the
long-name entries that fat_write does for files. Make bowls get long names
the same way (and their alias), and test with a space and with a name
longer than 8.3, checked with mtools and fsck.

## 4. seq.elf — DONE v0.57.7

`seq LAST`, `seq FIRST LAST`, `seq FIRST STEP LAST`, integers, negatives
and counting down included. Compare with the host's seq.

## 5. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

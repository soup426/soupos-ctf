# Night queue

Twenty-second queue. The first twenty-one are in `docs/queue-archive-2026-10-0*.md`.
Two items wait on a person, with notes in their archives: the seventh's
syscall-pointer item and the thirteenth's swap-shares item. The
twenty-first gave scripts for and if and added cmp; this one adds
arithmetic, while loops (and a way out of them), and script arguments.

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

## 1. Arithmetic — DONE v0.60.0

$(( EXPR )) with integers: + - * / % and parentheses, unary -, the
comparisons < <= > >= == != (1 or 0), $NAME and bare NAME for variables,
division by zero an error. Compare with sh over a table of expressions.

## 2. while loops, and Ctrl-C stops a loop — DONE v0.60.1

`while COMMANDS ; do COMMANDS ; done`, as sh does. A shell loop runs no
program between some commands, so Ctrl-C must also be noticed by the loop
itself: pressing it stops a for or while (and the rest of its line), and
the shell carries on. Compare a counting loop with sh; test Ctrl-C on
`while cook test.elf 1 = 1 ; do cook echo.elf x > /dev/null ; done`.

## 3. Script arguments — DONE v0.60.2

`run FILE a b c`: $1 to $9, $# and $@ inside the script, as sh gives them
to `sh FILE a b c`. Compare a script's results with sh's.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

# Night queue

Sixteenth queue. The first fifteen are in `docs/queue-archive-2026-10-0*.md`.
Two items wait on a person, with notes in their archives: the seventh's
syscall-pointer item and the thirteenth's swap-shares item. The fifteenth
added uniq, wc's flags, tee and cut; this one adds tr, then teaches the
shell to chain commands on their exit codes, which the tools now give
honestly.

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

## 1. tr.elf — DONE v0.56.9

Translate one set of characters to another, with ranges (a-z) and the
shorter set padded with its last character as tr does; -d deletes the set;
-s squeezes runs. Compare with the host's tr in the C locale.

## 2. ; && and || in the shell — DONE v0.57.0

`a ; b` runs both, `a && b` runs b only if a exited 0, `a || b` only if it
did not. Left to right, as sh groups them. A builtin's status counts too
(0, or 1 when it refused). Compare the sequence of what ran with the host
shell running the same commands.

## 3. $? in the shell — DONE v0.57.1

The last command's exit status, substituted where `$?` appears in a
command line. Test it after a program that exits 0, one that exits
non-zero, one killed, and a builtin that refused.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

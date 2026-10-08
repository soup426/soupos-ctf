# Night queue

Twelfth queue. The first eleven are in `docs/queue-archive-2026-10-0*.md`.
The seventh's notes still hold the set-aside syscall item for a person to
decide on. The eleventh gated what a cook may do and closed the pass door's
hijack; this one looks at the brakes a determined cook could still get
around.

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

## 1. chef is a secret-guessing oracle without brakes — DONE v0.55.2

`chef <command>` asks for the headchef's secret. Logins over the vault and
the pass now cost time per wrong secret and are recorded; chef may do
neither. Measure how many guesses a cook gets a minute through it, then give
it the same pause and a record in /etc/logins.

## 2. One cook can fill the process table — DONE v0.55.3

Nothing limits how many processes one cook runs. Find the table's size,
prove a cook can fill it so that nobody else (the headchef included) can
cook anything, then add a per-cook limit (the headchef exempt) and test it.

## 3. The login record can be flushed — DONE v0.55.4

/etc/logins keeps the newest 64 lines, so refusals push real logins out.
Measure how long a determined guesser needs to flush a login out through
each door, and decide (and write down why) between keeping more lines,
keeping successes apart from refusals, or both.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

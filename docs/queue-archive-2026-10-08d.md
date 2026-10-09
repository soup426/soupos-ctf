# Night queue

Eleventh queue. The first ten are in `docs/queue-archive-2026-10-0*.md`.
The seventh's notes still hold the set-aside syscall item for a person to
decide on. The tenth sealed the files that hold secrets; this one looks at
what a cook may DO, and at the other door into the kitchen.

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

## 1. Which commands a cook may run — DONE v0.55.0

Only kill, hire, fire and secret have been looked at for who may run them.
Go through every shell command for actions that belong to the headchef:
opening or closing the vault or the pass door, rekey and idle settings,
network changes, anything that writes the disk wholesale or reboots. Prove
each one an ordinary cook can wrongly run on the current kernel before
gating it, and test them together.

## 2. The pass door gets the same rules as the vault — DONE v0.55.1

`pass` is the clear-text door (telnet-style). The vault records logins in
/etc/logins, slows guessing and locks out an address, ends idle sessions.
Find out which of those apply to pass sessions (measure, do not assume),
and give it the ones it lacks.

## 3. The full check waits on cooks-test — DONE (scripts only): 237 s to 134 s

cooks-test is 226 of the full check's 237 s. Split it so the check is no
slower than its next-longest test plus a little, and record the before and
after.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

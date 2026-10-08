# Night queue

Twenty-third queue. The first twenty-two are in `docs/queue-archive-2026-10-0*.md`.
Two items wait on a person, with notes in their archives: the seventh's
syscall-pointer item and the thirteenth's swap-shares item. The
twenty-second added arithmetic, while (with Ctrl-C stopping a loop) and
script arguments; this one lets a script use a program's output and the
cook's input, and gives each cook a profile.

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

## 1. Command substitution — DONE v0.60.3

`$(COMMAND)` is replaced by what COMMAND (a program or pipeline, through
the shell as usual) writes to stdout, trailing newlines removed, as sh
does; not inside '...'. Nested is not needed. Compare with sh, e.g.
`N=$(cook wc.elf -l < F)`, a pipeline's output, and an empty result.

## 2. read VAR — DONE v0.60.4

`read NAME` reads a line from the terminal (console or session) into a
variable; at end of input the status is 1. Test from a script that asks
and the console that answers.

## 3. ~/profile at login — DONE v0.60.5

If the cook's home holds a file named profile, it is run (as `run` would)
at each interactive login - console, pass, vault, not `ssh host cmd` -
with the cook's permissions. An error in it does not stop the login.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

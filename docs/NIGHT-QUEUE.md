# Night queue

Twenty-seventh queue. The first twenty-six are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Programs get somewhere to send their complaints, an
environment and two more tools; the shell gets functions.

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

## 1. 2> sends a program's complaints somewhere

`cook X.elf 2> ERR.TXT`, `2>> ERR.TXT` and `2>&1` for a program's stderr
(fd 2), on any stage of a pipeline, as sh does. Today stderr always goes to
the screen. Compare what lands where with bash running the host tools.

## 2. Shell functions

`name() { cmd ; cmd ; }` on one line defines one; `name a b` runs it with
$1..$9, $# and $@ set for the body and restored after; its status is the
last command's. Compare with bash.

## 3. stack.elf

Lines in reverse order, as `tac` does (a last line with no newline joins
the one after it, as tac's output shows). Compare with the host's tac.

## 4. spread.elf

Tabs to spaces, as `expand` does (-t N, default 8). Compare with the host's
expand.

## 5. rack sorts by a field, once

`-u` (one of each line the sort calls equal), `-t SEP` and `-k N` (sort by
field N), with the existing -n and -r. Compare with the host's sort.

## 6. export: programs get an environment

`export NAME` (and `export NAME=value`) marks a variable; every program a
cook runs gets the marked ones as NAME=value strings, read through a new
ulib getenv(). A program to list them (a kitchen name). Test that only
exported ones arrive and that unset removes one.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

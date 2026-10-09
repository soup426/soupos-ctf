# Night queue

Fortieth queue. The first thirty-nine are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). A stack of bowls, a way past functions and nicknames to
the builtin, rest -n, Tab for variables, and select.

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

## 1. shelve, unshelve, shelf (pushd, popd, dirs) — DONE v0.60.99

`shelve BOWL` goes there and keeps where it was on a stack; `unshelve`
goes back to the top one; `shelf` lists the stack (the current bowl
first, as dirs). Kitchen names. Compare with bash's pushd/popd/dirs.

## 2. plain CMD (command) — DONE v0.60.100

Runs CMD as the builtin or the program it names, skipping a function or a
nickname of the same name, as sh's command does; `plain -v NAME` says
what it would run. A kitchen name. Compare with bash's command.

## 3. rest -n — DONE v0.60.101 (with SYS_SLEEP and marinate.elf)

Waits for the next background job to end, whichever it is, its status
being that job's, as wait -n. Test with two jobs of different lengths.

## 4. Tab finishes $NAME — DONE v0.60.102

A word starting with $ (or ${) finishes from the shell's variable names,
as bash's completion does. Test by typing.

## 5. select NAME in WORDS ; do ... ; done — DONE v0.60.103

sh's select: a numbered menu of WORDS, a prompt ($PS3, or "#? "), the
answer read; NAME is the chosen word (empty for a number out of range),
REPLY what was typed; round again until break or the end of input.
Test by typing.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

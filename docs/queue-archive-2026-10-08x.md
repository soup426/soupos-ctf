# Night queue

Thirty-first queue. The first thirty are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). The menu catches up with the shell, globs learn
brackets, functions get their own variables, scripts can stop at the
first failure, and $RANDOM.

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

## 1. The menu matches the shell — DONE v0.60.54

`menu` still lists names from before the kitchen renames (jobs, clip, and
perhaps more) and may miss builtins added since. Every command it names
must run (no "No soup for you"), and every builtin the dispatcher has
must be listed. Test it: take the names from menu's own output and try
each.

## 2. [abc] and [a-z] in globs and case patterns — DONE v0.60.55

glob_match knows * and ? only. Add bracket expressions, ranges and [!x],
for file globs and case patterns alike. Compare with bash.

## 3. stash (local) — DONE v0.60.56

`stash NAME[=value]` inside a function: the variable is the function's
own, the caller's (or its absence) back when it returns, as sh's local.
A kitchen name, since local is a builtin, not a keyword. Compare with
bash, nested calls and a recursive function included.

## 4. follow -e — DONE v0.60.57

`follow -e FILE` stops the script at the first command that fails (not
counting a condition of if/while/until, or a command before && or ||),
with that command's status, as `bash -e` does. Compare with bash -e.

## 5. $RANDOM — DONE v0.60.58

A number from 0 to 32767, new at each expansion, from the entropy pool.
Test that it stays in range and does not repeat in a run of a hundred.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

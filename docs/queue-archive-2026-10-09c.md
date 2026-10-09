# Night queue

Thirty-seventh queue. The first thirty-six are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). slurp learns echo's -n and -e, variables learn case
and indirection, $(< FILE), a timer for commands, and arrays.

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

## 1. slurp -n and -e — DONE v0.60.84

echo's -n (no newline at the end) and -e (\n \t \\ and the rest), and -E,
in bash's way (options only first, -ne together, a word like -x printed).
Compare with bash's echo.

## 2. ${NAME^} ${NAME^^} ${NAME,} ${NAME,,} and ${!NAME} — DONE v0.60.85

Upper and lower case, the first letter or all of them, and indirection
(the variable whose name NAME holds). Compare with bash.

## 3. $(< FILE) — DONE v0.60.86

The file's contents, as $(spoon.elf FILE) gives them but without starting
a program, as bash. Compare with bash.

## 4. eggtimer CMD (time) — DONE v0.60.87

Runs CMD and says how long it took, real time in seconds to the
hundredth, as bash's time says real (no user and sys here). A kitchen
name. Test that CMD's output and status are untouched and the time is
there and sane.

## 5. Arrays — DONE v0.60.88 (dense; see ROADMAP)

`a=(x y z)`, ${a[i]} (i arithmetic), ${a[@]} and "${a[@]}" (a word each),
${#a[@]}, a[i]=v, and for over them. Compare with bash.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

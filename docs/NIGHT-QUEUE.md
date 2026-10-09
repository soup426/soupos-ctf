# Night queue

Thirty-ninth queue. The first thirty-eight are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Builtins learn redirections at last, the shell's own
variables, take's options, case's fall-through, and array keys.

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

## 1. Redirections on builtins and functions — DONE v0.60.94

`slurp hi > /f.txt` prints "hi > /f.txt" today: only cook and a loop's
done (or a { } group) take > F. A builtin's or a function's line takes
> F, >> F and < F as `{ cmd ; } > F` would, wherever the redirection
sits in the line. Compare the files with bash's.

## 2. $$, $PPID, $SECONDS, $LINENO — DONE v0.60.95

The shell's own number, its parent's, the seconds since it started (and
SECONDS=N resets the count from N), and the line of the follow script
being run. Test against what is known (two reads of $$ agree, $SECONDS
moves with sleeptest, $LINENO counts a script's lines as bash does).

## 3. take -p PROMPT, -n N, -t SECS — DONE v0.60.96

read's prompt, its N characters and no more, and its timeout (status
above 128 when it runs out, as bash). Test by typing and by not typing.

## 4. case ;& and ;;& — DONE v0.60.97

;& runs the next arm's commands too, without testing it; ;;& goes on
testing the arms after. Compare with bash.

## 5. ${!a[@]} and ${!prefix*} — DONE v0.60.98

An array's indexes, and the names of the variables starting with prefix.
Compare with bash.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

# Night queue

Forty-seventh queue. The first forty-six are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). bash's C-style for loop and read -d, then what nl,
cmp, paste and seq can do that label, pair, layer and tally cannot (each
checked against the menu and the programs first).

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
13. **Guard every new file**: `[ -e scripts/X-test.sh ] && { echo EXISTS;
    exit 1; }` before writing it (v0.60.128 wrote over headtail-test.sh
    after an `ls` printed that it was there).
14. **Check it is not already there**: grep the menu for "(sh's NAME)"
    and read the first lines of user/*.c before queuing an sh feature or
    a Unix tool (queue 42 queued wait and getopts, already rest and pluck).
15. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. for (( INIT ; COND ; STEP )) ; do ... ; done — DONE v0.60.136

bash's arithmetic for loop (a keyword form, its own name): INIT once,
COND before each pass (empty is true), STEP after; break, continue and N
of each; nested; over several lines and in a recipe; its status the
last command's (0 if none ran). Test against bash.

## 2. take -d DELIM (read -d) — DONE v0.60.137

Read up to DELIM instead of a newline (its first character; -d '' to the
end of input, as bash reads to a NUL), with -r -a -n -p and from a pipe,
<<< and < FILE. Test against bash.

## 3. label: nl's options — DONE v0.60.138

-b a (number every line) or t (non-empty, the default) or n, -w WIDTH,
-s SEP, -v START, -i STEP, -n ln, rn or rz. Compare with GNU nl (single
logical page; no section delimiters).

## 4. pair -l, -s and the EOF message (cmp) — DONE v0.60.139 (the EOF message was already GNU's)

-l every differing byte (number, then both values in octal), -s nothing
said, only the status; a shorter file: "cmp: EOF on NAME after byte N,
line M" as GNU's (on stderr), status 1. Compare with GNU cmp.

## 5. layer -s and - (paste) — MOVED to queue 48's end (the desktop first, at the user's request)

-s each file's lines joined into one line (with -d's list cycling); - for
stdin, more than once taking turns. Compare with GNU paste.

## 6. tally with decimals (seq) — MOVED to queue 48's end

FIRST, STEP and LAST may have fractions: computed in scaled integers
(user programs have no floating point) to the most decimals any of them
has, printed with that many, as GNU seq does (seq 0 0.5 2 gives 0.0 to
2.0); negatives, a step that does not land on LAST, -w and -s with
them. Compare with GNU seq.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Skipped at the user's request: OS work only.

## Notes left by unattended runs

(none yet)

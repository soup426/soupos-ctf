# Night queue

Forty-second queue. The first forty-one are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). A quoting bug the forty-first found, then three
builtins scripts reach for (eval, wait, getopts) and two programs
(comm and factor).

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

## 1. "$(cmd)" keeps its newlines — DONE v0.60.109

Found in v0.60.105: inside double quotes, `"$(cmd)"` turns cmd's inner
newlines into spaces, where bash keeps them (only trailing newlines go).
`slurp "$(cook stalk.elf a/b c/d)"` should be two lines. Unquoted `$(cmd)`
still splits into words. Prove it first, then a test against bash: two
and three lines, trailing newlines stripped, assigned to a variable
(`x=$(...)` keeps them too), and inside a longer quoted string.

## 2. brew WORDS (eval) — DONE v0.60.110

Its words joined by spaces and run as a command line, status that line's.
Under a kitchen name (eval is a special builtin in POSIX, but the
kitchen-names rule wins). Test against bash: building a variable name
(`brew "v$i=x"`), a pipeline in a string, `;` and `&&` inside, quoting
that survives one round, nested brew, and its status.

## 3. tend [%N|PID] (wait) — CLOSED, already there

Measured before building: `rest [pid|%N|-n]` has been sh's wait since
v0.60.48 (v0.60.101 added -n), and rest-test, restn-test and jobspec-test
already cover every case listed below ($!, a job's exit code, no
argument, no such job 127, nothing running, %1, -n). Nothing to build.


Waits for a background job (or all of them with no argument), status
the job's exit code (127 for one that does not exist, as bash). Measure
how jobs are tracked first (job_spec, job_pid). Test: `cook marinate.elf
1 & tend ; slurp $?`, a job that exits 3, `tend %1`, `tend` with nothing
running, `$!`.

## 4. prep OPTSTRING NAME (getopts) — CLOSED, already there

Measured before building: `pluck OPTS NAME [ARGS]` has been sh's getopts
since v0.60.77, with OPTARG, OPTIND, bundling, -- and the leading-: silent
mode (pluck-test). Nothing to build.


getopts under a kitchen name: one option per call from $1.. (or the words
given), NAME set to the letter, OPTARG to its value for `x:`, OPTIND
advanced; `?` for an unknown option, status 1 at the end; `-ab` bundled,
`--` ends. A leading `:` in OPTSTRING is silent mode. Compare a
`while prep ...; do case ...` loop with bash's getopts across a dozen
argument lists, in a function and in a recipe.

## 5. potluck.elf (comm) — DONE v0.60.111

What two sorted files brought: lines only in the first, only in the
second, and in both, in three tab-indented columns; -1 -2 -3 drop
columns (and combine, -12). Compare with the host's comm on files with
repeated lines, an empty file, and stdin as -.

## 6. portion.elf (factor) — DONE v0.60.112

N: its prime factors in order, a line per number, from the words or
stdin, as GNU's factor. Find out whether ulib can divide 64-bit numbers
first (libgcc's __udivdi3); if not, 32-bit and say so in the usage. Test
against the host's factor: 1, primes, squares, 2^31-1, 4294967295, and
bad input (status 1).

## 7. Doom at the framebuffer's own resolution — DEFERRED

Skipped at the user's request: OS work only.

## Notes left by unattended runs

- Queue 42 was written checking names against cmd_names, not what the
  commands do, so items 3 and 4 turned out to exist already (rest, pluck).
  Before queuing an sh builtin, grep the menu for "(sh's NAME)".

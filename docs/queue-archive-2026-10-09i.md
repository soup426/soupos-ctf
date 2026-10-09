# Night queue

Forty-third queue. The first forty-two are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Each item below was checked against the menu and the
programs first. Two pieces of syntax scripts lean on ([[ ]] and function),
the shell's own switches (set), the jobs list, xargs, and shuf.

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
13. **Check it is not already there**: grep the menu for "(sh's NAME)"
    and read the first lines of user/*.c before queuing an sh feature or
    a Unix tool (queue 42 queued wait and getopts, already rest and pluck).
14. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. [[ EXPR ]] — DONE v0.60.113 (no =~)

bash's conditional command, a keyword so it keeps its name: no word
splitting or globbing of what is inside, `==` and `!=` against a glob
pattern (an unquoted right side is a pattern, a quoted one is text), `<`
and `>` comparing strings, `&&`, `||`, `!` and ( ) grouping, and taste's
unary and binary tests (-f -d -e -z -n -eq ...). `=~` only if it measures
cheap (sift has no regex): otherwise left out and said so. Test against
bash: a variable holding spaces unquoted, patterns, && || ! ( ), in if,
while and with && after it.

## 2. function NAME { ...; } and function NAME() { ...; } — DONE v0.60.114

The other way bash writes a function; a keyword, so it keeps its name.
Both forms, one line and over several, in a recipe, with stash, return
and $1; inspect says it is a function. Test against bash.

## 3. temper (set) — DONE v0.60.115

sh's set under a kitchen name: `temper -e` / `+e` (errexit, which today
only `follow -e` turns on), `-u` (an unset variable is an error, status 1
and the command does not run), `-x` (each command written to stderr with
a +, after expansion), `temper -- A B C` and `temper A B C` setting $1..
and $#, `temper` alone listing variables. Test each against bash's set.

## 4. rail (jobs) — DONE v0.60.116

The jobs this shell put in the background or set aside, as bash's jobs:
`[N]+  Running   cmd &`, `-` for the one before, Stopped and Done (a done
job shown once, then gone), -p for the PIDs only. Measure what job_add
and the prompt's reporting already keep.

## 5. runner CMD (xargs) — DONE v0.60.117

Lines (or blank-separated words) from stdin added as arguments to CMD,
as xargs: in batches that fit (a program's arguments are 128 bytes at
most), -n N per command, -I {} once per line with {} replaced; status
123 if any run failed, as GNU. Measure first how a builtin reads a pipe
(`| while take` works). Test against xargs with programs and builtins.

## 6. tumble.elf (shuf) — DONE v0.60.118

The lines of a file or stdin in a random order, as shuf; -n N the first N
of them; -i LO-HI the numbers instead. Ring 3 has no randomness today:
add SYS_RANDOM from the kernel's entropy pool (what $RANDOM uses). Test
properties, not bytes: every line out once, -n N lines, two runs differ,
and the CHALLENGE=1 build unaffected.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Skipped at the user's request: OS work only.

## Notes left by unattended runs

(none yet)

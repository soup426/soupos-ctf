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

## 1. 2> sends a program's complaints somewhere — DONE v0.60.21

`cook X.elf 2> ERR.TXT`, `2>> ERR.TXT` and `2>&1` for a program's stderr
(fd 2), on any stage of a pipeline, as sh does. Today stderr always goes to
the screen. Compare what lands where with bash running the host tools.

## 2. Shell functions — DONE v0.60.22

`name() { cmd ; cmd ; }` on one line defines one; `name a b` runs it with
$1..$9, $# and $@ set for the body and restored after; its status is the
last command's. Compare with bash.

## 3. stack.elf — DONE v0.60.23

Lines in reverse order, as `tac` does (a last line with no newline joins
the one after it, as tac's output shows). Compare with the host's tac.

## 4. spread.elf — DONE v0.60.24

Tabs to spaces, as `expand` does (-t N, default 8). Compare with the host's
expand.

## 5. rack sorts by a field, once — DONE v0.60.25

`-u` (one of each line the sort calls equal), `-t SEP` and `-k N` (sort by
field N), with the existing -n and -r. Compare with the host's sort.

## 6. export: programs get an environment — DONE v0.60.26 (as `hand`, with labels.elf)

`export NAME` (and `export NAME=value`) marks a variable; every program a
cook runs gets the marked ones as NAME=value strings, read through a new
ulib getenv(). A program to list them (a kitchen name). Test that only
exported ones arrive and that unset removes one.

## 8. A builtin or a function into a pipe — DONE v0.60.27

Found with item 2: `up loud | raise.elf`, where up is a function (or
`slurp hi | raise.elf`, a builtin), hands `|` and `raise.elf` to it as
words; only `cook` lines make pipes. Run the left side with its output
captured (the $(...) capture terminal is the start) and feed it to the
program as stdin. Compare with bash.

## 9. music-test judges fixed seconds of the capture — DONE v0.60.28

2026-10-08: one full check (the first for v0.60.23) failed music-test with
"the music and the effects did not coexist"; the rerun passed, and twelve
runs six at a time passed (with effects 2219-2349 against a bar of 1807).
The test compares the capture's mean amplitude over fixed windows (1.0-3.5 s
music, 5.0-6.5 s with effects, 7.5-9.0 s music again); under a full check's
load the effects can start late and miss theirs. Find the effects' onset in
the capture (or mark it on the serial log) and measure around that instead.
The failing run's numbers were not kept: have the test print them on FAIL.

## 10. pour as cat — DONE v0.60.29

Found with item 8: pour shows at most 8 KB (then a "truncated" note),
drops bytes outside printable ASCII, and always adds a newline, so
`pour X | prog` is not `cat X | prog`. Make it read the whole file in
pieces and write it as it is (CLAUDE.md and the tests that read pour's
output may lean on the extra newline: check). Add the pour case back to
bpipe-test.

## 11. Long word lists — DONE v0.60.30

Found with item 8: `for i in $(cook tally.elf 6000)` loops 64 times, not
6000: a command is expanded into INPUT_MAX (256) bytes, so the $(...)'s
words are cut. sh has no such limit. Expand a for loop's word list (and
$(...) generally) into something larger, and say so when it is cut.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

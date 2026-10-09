# Night queue

Thirty-sixth queue. The first thirty-five are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). A test that needs no program, builtins that read
here-strings, a prompt that waits for the rest of an if, smoke (trap),
and here-documents inside a script's multi-line blocks.

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

## 1. taste and [ ... ] as builtins — DONE v0.60.79

Conditions start a program today (`cook taste.elf`). taste (test) as a
builtin, the same operators as taste.elf, and `[ EXPR ]` the same with a
closing ] required, as sh. Compare with bash's test and [.

## 2. Builtins read <<< and <<WORD — DONE v0.60.80

`take a <<< x` reads the terminal (v0.60.76 noted it). A here-string or a
here-document on a builtin's line, take above all, is its input. Compare
with bash.

## 3. The prompt waits for the rest — DONE v0.60.81

`for x in a b` and Enter runs nothing useful today. An unfinished if,
for, while, until, case or { asks for more lines with a > prompt until it
closes, as sh, then runs the whole; Ctrl-C drops it. Test by typing.

## 4. smoke 'CMD' SIGNAL... (trap) — DONE v0.60.82

sh's trap under a kitchen name (the smoke alarm): EXIT runs CMD when a
script (or a clockout) ends, INT when Ctrl-C stops a script; `smoke ''
SIG` ignores, `smoke - SIG` resets, bare `smoke` lists. Compare with
bash where bash can be driven the same way.

## 5. Here-documents inside a script's multi-line blocks — DONE v0.60.83

follow joins a compound's lines (v0.60.74) but skips <<WORD inside one.
Read the document's lines where they are, inside the block. Compare
with bash.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

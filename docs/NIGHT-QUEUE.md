# Night queue

Thirty-eighth queue. The first thirty-seven are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Brace expansion, nicknames (aliases), the dot command,
job numbers, and history expansion at the prompt.

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

## 1. Brace expansion — DONE v0.60.89

{a,b,c} and {1..5}, {a..e}, {5..1}, {01..10} and a step {1..10..2},
nested and with a prefix and suffix (x{a,b}y), not in quotes, before the
other expansions, as bash. Compare with bash.

## 2. nickname NAME=TEXT (alias) — DONE v0.60.90

The first word of a command that is a nickname is replaced by its text
(again, for the text's own first word, but not a nickname inside
itself); bare `nickname` lists them, `nickname NAME` shows one, and
`forget NAME` removes one (unalias). A kitchen name. Compare with bash's
alias where bash can be driven the same way (bash -i or shopt -s
expand_aliases).

## 3. . FILE — DONE v0.60.91

POSIX's dot command, a special builtin, so it keeps its name: follow FILE
in this shell, as follow already does. Compare with bash's . on a script
that sets variables.

## 4. Job numbers — DONE v0.60.92

kill, plate, steep and rest take %N (the Nth background job, as orders
lists them), %% and %+ (the newest), %- (the one before); orders shows
the numbers. Test with two jobs.

## 5. History expansion at the prompt — DONE v0.60.93

!! (the last line), !N, !-N, !word (the last line starting so), and !$ (the
last word of the last line), shown before they run, as bash does at its
prompt. Test by typing.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

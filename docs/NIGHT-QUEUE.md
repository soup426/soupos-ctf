# Night queue

Forty-fourth queue. The first forty-three are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Checked against the menu and the programs first: no
regular expressions anywhere yet (sift is fixed strings, [[ =~ ]] was left
out), no read -a or mapfile, no diff, split or expr.

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

## 1. Regular expressions, and sift -E — DONE v0.60.119

A small POSIX ERE matcher in one file both sides can build (user/rx.c,
included by programs and by the kernel): literals, `.`, `[...]` with ranges
and `[^...]`, `*` `+` `?`, `{m,n}`, `^` `$`, `|`, `( )`, `\` escapes; a
backtracking matcher is fine at these sizes, with a step limit so a bad
pattern cannot hang a program. `sift -E PATTERN` uses it, with -v -c -i -n
-o as sift already has them. Test against GNU grep -E over a list of
patterns and lines (probe with `command grep`, not the ugrep wrapper).

## 2. [[ STRING =~ REGEX ]] and BASH_REMATCH — DONE v0.60.120

The same matcher in the shell: status 0/1/2 as bash's, the whole match in
BASH_REMATCH[0] and each ( ) group in [1].. (an array), an unquoted right
side a regex and a quoted one literal text. Test against bash.

## 3. take -a NAME and stock NAME (read -a, mapfile) — DONE v0.60.121

`take -a NAME` reads a line's words into the array NAME. `stock [-t]
NAME` (mapfile under a kitchen name) reads every line of its input into
NAME, -t dropping the newlines. Both from a pipe (as runner reads one)
and from <<<. Test against bash.

## 4. spot.elf (diff) — DONE v0.60.122

What changed between two files, in diff's normal format (`2c2`, `< `,
`---`, `> `, `a` and `d` with their ranges), from a longest common
subsequence; status 0 same, 1 different, 2 trouble. Compare with GNU diff
on edits at the start, middle and end, insertions, deletions, an empty
file, and identical files.

## 5. carve.elf (split) — DONE v0.60.123

A file (or stdin) into pieces: -l N lines (1000 by default) or -b N
bytes, named PREFIX then aa, ab, ... (x by default). Compare the pieces
with GNU split's byte for byte.

## 6. reckon.elf (expr) — DONE v0.60.124

expr under a kitchen name: + - * / % on integers, = != < <= > >= (as
numbers when both are, else as text), | and &, length, substr, index,
and STRING : REGEX through item 1's matcher. Output and status (0, 1 for
a null or 0 result, 2 for a bad expression) as GNU's expr. Compare.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Skipped at the user's request: OS work only.

## Notes left by unattended runs

- v0.60.124: SYS_READDIR runs fat_ls over the whole directory for every
  entry it hands back, so peek.elf on the 79-entry root lists 79 x 79; it
  ran past ls-test's flat two-second wait under a loaded check. The test
  now waits for the prompt; the quadratic listing is worth an item.

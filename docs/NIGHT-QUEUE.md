# Night queue

Twenty-eighth queue. The first twenty-seven are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Two limits written down last queue get fixed, the shell
learns sh's commonest idiom, and two tools get the flags people reach for.

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

## 1. 2>&1 > f, in sh's order — DONE v0.60.31

v0.60.21 made 2>&1 a flag meaning "wherever stdout ends up", so
`2>&1 > f` sends stderr to f; sh sends it to the terminal (the duplicate
is taken before > moves stdout). Keep the order the redirections were
written in. Compare both orders with bash.

## 2. cd -, $PWD and $OLDPWD — DONE v0.60.32

`cd -` goes back to the previous bowl and prints it, as sh does; $PWD and
$OLDPWD expand to the current and previous bowls. Compare with bash.

## 3. A builtin's pipe streams — DONE v0.60.33

v0.60.27 runs the left side of `builtin | prog` to the end before the
right side starts, so a left side that never ends holds the line. Start
the right side first, feed it as the left side prints, and wait for it as
cook does. Test with a left side that prints more than a pipe holds and a
right side that stops early.

## 4. A program into a shell loop — DONE v0.60.34

`cook spoon.elf F | while take line ; do slurp got $line ; done`: the right
side of a pipe is the shell itself, reading the program's output as its
input (take reads the pipe, not the keyboard). Compare with bash's
`cat F | while read line; do echo got $line; done`.

## 5. sift -n and -o — DONE v0.60.35

-n puts the line number before each line, -o prints only the matching part
(each match on a line of its own). Compare with the host's grep.

## 6. tally -s and -w — DONE v0.60.36

-s SEP between the numbers instead of a newline, -w pads them to equal
width with zeros. Compare with the host's seq.

## 8. A whole loop into a pipe — DONE v0.60.37

Found with item 3: `while slurp y ; do x=1 ; done | skim.elf -n 3` takes
`done | skim.elf ...` for the loop's done, so the pipe is dropped without a
word and the loop prints to the screen for ever (Ctrl-C stops it). sh pipes
the whole compound. Pipe for, while, if and case when their closing word
has `| ...` after it (the feed exec_ranges the compound instead of a line),
and refuse anything after done/fi/esac that cook would not take. Compare
`for ... done | rack.elf` and `while ... done | skim.elf -n 3` with bash.

## 9. Word splitting of an unquoted $VAR — DONE v0.60.38 (it was slurp's alone)

Found with item 4: line="a  b" ; `slurp x:$line` prints "x:a  b" where
bash's `echo x:$line` prints "x:a b": sh splits an unquoted expansion into
words at blanks, and a builtin that joins its words gets one space between
them. soupOS keeps the expansion as it is. Decide where splitting belongs
(expand_seg marks the text from expansions; the builtins that take words
split there) and compare echo/slurp, for and a program's args with bash.

## 10. Quotes that come out of an expansion are text — DONE v0.60.39

Found with item 9: v="it's" ; `slurp $v` prints "its", and `cook call.elf
$v` gets an unbalanced quote: expand_seg puts the value into the line as
it is, and the quote handling after it (uargv, slurp, first_word) reads its
' and " as quotes. sh never re-reads an expansion's quotes. Mark expanded
quote characters (or escape them) so only typed quotes group words;
compare with bash for $v, $(...) and $1 holding quotes.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

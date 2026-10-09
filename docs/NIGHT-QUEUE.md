# Night queue

Thirtieth queue. The first twenty-nine are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Scripts get the rest of what they lean on: leaving a
function or a script early, background jobs to wait for, printf, (( )),
and here-documents.

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

## 1. return and exit — DONE v0.60.47

`return [N]` leaves a function with status N (the last command's if no
N); `exit [N]` ends a `follow` script with status N (at the prompt it is
clockout's business, so say so and do nothing there). sh's keywords keep
their names. Compare with bash.

## 2. $! and rest (wait) — DONE v0.60.48

`$!` is the id of the last program started in the background; `rest`
(wait, under a kitchen name) waits for all of this cook's background
jobs, or for the one given, and its status is that job's. Compare the
statuses with bash.

## 3. dish.elf (printf) — DONE v0.60.49

`printf FORMAT ARGS...` under a kitchen name: %s %d %i %x %o %c %%, widths,
`-` and `0` flags, precision for %s, \n \t \\ escapes, and the format
reused while arguments remain. Compare with the host's printf byte for
byte.

## 4. (( expr )) — DONE v0.60.50

The arithmetic command: status 0 when the expression is not zero, 1 when
it is, with the $(( )) evaluator; `n=$((n+1))` gets `(( n = n + 1 ))`
and `(( n++ ))`, `(( n += 2 ))` too. Compare with bash.

## 5. Here-documents in scripts — DONE v0.60.52

In a `follow` script, `cook prog <<WORD` takes the lines up to WORD as
the first stage's stdin, $NAME expanded in them (not with <<'WORD').
Compare with bash running the same script.

## 7. music-test: "the effects never showed" — DONE v0.60.51 (likely cause; reopen if it recurs)

2026-10-08, the first full check for v0.60.48: "the effects never showed
in the capture (music 2466, 10.2 s captured)"; the rerun passed. The
music alone measured 2466, about twice the usual 1291, and the capture
was 10.2 s. v0.60.28 finds the effects as the first quarter second half
again as loud as the loudest music quarter second; a louder baseline
(the capture starting late, so 1.0-3.5 s holds the louder part of the
score) puts the bar above the effects. Again on v0.60.50's first check:
music 2987, 8.2 s captured. Measure where the capture starts
relative to `hum` (the serial log has both) and anchor the windows there.

## 8. Backslash in the shell — DONE v0.60.53

Found with item 3: sh treats \ as an escape: outside quotes it makes the
next character plain (\; \| \$ \  a space), inside "..." it does so for
\ " $ and ` (so "a\\c" is a\c), inside '...' it is just a character.
soupOS gives \ no meaning anywhere. Teach the quote handling (split_segs,
expand_seg, uargv's caller, unquote, slurp) the same, and compare with
bash; a program's arguments are where it shows (dish, sift).

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

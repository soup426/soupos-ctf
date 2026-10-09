# Night queue

Twenty-ninth queue. The first twenty-eight are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). The shell grows the pieces of sh that scripts reach for
next: assignments that keep their value, a variable for one command, the
${...} forms, until, break and continue, and here-strings.

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

## 1. w=$v keeps the whole value — DONE v0.60.40

v="a b" ; w=$v : bash sets w to "a b" (an assignment's value is not split);
soupOS expands the line to `w=a b`, which is no longer an assignment, and
runs `b`. Expansions after NAME= at the start of a command must stay one
value (the $(...) case already wraps itself; do the same for $NAME, $1..).
Compare with bash.

## 2. A=1 cook prog: a variable for one command — DONE v0.60.41

`NAME=value cook prog` hands NAME=value to that program only (its
environment), leaving the shell's NAME as it was; several prefixes may
come first. try_assign says this is not supported; support it. Compare
with bash's `NAME=value env | grep NAME` and the shell's value after.

## 3. ${VAR:-word}, ${VAR:=word} and ${#VAR} — DONE v0.60.42

The default when unset or empty, the default that also sets it, and the
length. Compare with bash.

## 4. until loops — DONE v0.60.43

`until COMMANDS ; do ... ; done`, while's opposite. Compare with bash.

## 5. break and continue — DONE v0.60.44

In for, while and until, with an optional N for nested loops. Compare
with bash.

## 6. <<< here-strings — DONE v0.60.45

`cook rack.elf <<< "c b a"`: the word, with a newline, as the first
stage's stdin. Compare with bash.

## 8. A loop, a program, then a loop: done | prog | while ... — DONE v0.60.46

Found with item 4: `until ... ; done | stack.elf | while take x ; do ... ;
done` hangs the shell (the same with while). split_segs looks for the |
before a compound first, so "done | stack.elf" becomes a segment that
pipes into the second loop, and the first loop's done never gets its out.
sh runs the three as one pipeline. Make that work (the middle is a cook
line between two shell sides), or at least refuse it with a message
rather than hang; compare with bash.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

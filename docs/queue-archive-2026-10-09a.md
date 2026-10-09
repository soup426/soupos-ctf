# Night queue

Thirty-fifth queue. The first thirty-four are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). The shell still has no elif and no { } group; then
seal (readonly), pluck (getopts), and a check of "$@" and "$*".

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

## 1. elif — DONE v0.60.74 (and compounds over lines in scripts)

`if A ; then X ; elif B ; then Y ; else Z ; fi`, any number of elifs, as
sh. Compare with bash, nested ifs and follow scripts included.

## 2. { ... ; } groups — DONE v0.60.75

`{ a ; b ; }` runs a and b in this shell (variables stick), and the group
takes > F, >> F, < F, | PROG, && and || as a whole, as a loop's done
does. Compare with bash.

## 3. seal NAME[=value] (readonly) — DONE v0.60.76

A variable that cannot be set, discarded or stashed again; trying says
so and is status 1. A kitchen name. `seal` alone lists them. Compare
with bash's readonly.

## 4. pluck OPTSTRING NAME (getopts) — DONE v0.60.77

sh's getopts under a kitchen name: OPTIND, OPTARG, a : after a letter for
an argument, ? for one not known, a leading : for the quiet form; in a
script or a function, over $1..$9. Compare with bash.

## 5. "$@" and "$*" — DONE v0.60.78

Check them against bash in every place a word can be: arguments, for
lists, assignments, here-documents, with IFS's default; fix what
differs.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

- v0.60.76: `take a <<< x` reads the terminal; builtins do not get
  here-strings (or <<WORD). Worth an item in the next queue.

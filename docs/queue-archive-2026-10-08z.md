# Night queue

Thirty-third queue. The first thirty-two are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Variables learn to trim and replace, arithmetic gets
its bit operators and powers, take learns -r, and here-documents come to
the prompt.

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

## 1. ${NAME#pat} ${NAME##pat} ${NAME%pat} ${NAME%%pat} — DONE v0.60.64

Strip the shortest or longest match of a glob pattern from the front or
the back of a value, as sh. The pattern is a glob ([abc] included) and
may hold $X. Compare with bash.

## 2. ${NAME/pat/rep}, ${NAME//pat/rep}, ${NAME:off:len} — DONE v0.60.65

The first or every match replaced (# and % anchors too), and a slice.
Compare with bash.

## 3. & | ^ ~ << >> and ** in arithmetic — DONE v0.60.66

With their op= forms where sh has them, at sh's precedence. Compare with
bash.

## 4. take -r — DONE v0.60.67 (take was the -r one; plain take is read now)

take turns \x into x today, as read does; -r keeps backslashes, as
read -r. Compare with bash.

## 5. Here-documents at the prompt — DONE v0.60.68

`cook x.elf <<EOF` typed at the prompt asks for lines (a > prompt) until
EOF, as sh does; today only follow scripts have them. Test by typing.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

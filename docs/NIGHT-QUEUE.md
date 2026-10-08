# Night queue

Twenty-fifth queue. The first twenty-four are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Every command now has a kitchen name; this queue adds
two more tools and makes the prompt finish words for you.

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
8. **Kitchen names** for every new command, builtins and programs alike
   (sift.elf, not grep.elf); compare with the host tool under its real name.
9. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
10. **`make clean` leaves disk.img alone**: `rm disk.img && make disk` after
    changing anything the Makefile embeds.
11. Do not edit the tree while check.sh runs: it builds once at its start
    and reads each test script when that test starts.
12. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. flip.elf — DONE v0.60.8

Each line reversed, character by character, as rev does; a last line with
no newline keeps none. Compare with the host's rev.

## 2. label.elf

Lines numbered as `nl -ba` numbers them (every line, empty ones included,
the number right-aligned in six columns and a tab). Compare with the host.

## 3. Tab finishes a word at the prompt

Tab after part of the first word finishes it to a command name when only
one fits (a builtin, or a program on the disk); after a later word it
finishes a path in its bowl. More than one fit: complete the part they
share, and a second Tab lists them. Test by typing.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

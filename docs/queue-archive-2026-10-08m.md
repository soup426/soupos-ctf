# Night queue

Twentieth queue. The first nineteen are in `docs/queue-archive-2026-10-0*.md`.
Two items wait on a person, with notes in their archives: the seventh's
syscall-pointer item and the thirteenth's swap-shares item. The nineteenth
gave the shell variables and globs and added test.elf; this one gives
programs a stat call, which test.elf has had to do without.

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
8. **Kitchen names** for new shell commands.
9. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
10. **`make clean` leaves disk.img alone**: `rm disk.img && make disk` after
    changing anything the Makefile embeds.
11. Do not edit the tree while check.sh runs: it builds once at its start
    and reads each test script when that test starts.
12. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. SYS_STAT — DONE v0.59.0

Programs can open and list but not ask about a path. Add a stat call:
size, bowl or not, owner, mode, for a path the cook may reach (search on
the bowls above it; reading the file itself is not needed, as with stat).
test.elf then uses it, and its written-down difference (an unreadable file
counted as absent) goes away. Check against what perms and du say.

## 2. ls.elf -l — DONE v0.59.1

Mode, owner and size for each entry, through SYS_STAT, in the columns
`serve` uses. Check every line against perms for the same names.

## 3. find.elf — DONE v0.59.2

`find.elf [path] [-name PATTERN] [-type f|d]`: every name under a path,
one per line, bowls it may not enter skipped. Compare with the host's find
over the same tree, both sorted (FAT and ext4 list in different orders).

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

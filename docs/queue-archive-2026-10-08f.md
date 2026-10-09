# Night queue

Thirteenth queue. The first twelve are in `docs/queue-archive-2026-10-0*.md`.
The seventh's notes still hold the set-aside syscall item for a person to
decide on. The twelfth put brakes on guessing, process slots and the login
record; this one looks at the other things the cooks share.

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

## 1. One cook can fill the disk — DONE v0.55.5

A cook may write in their home and anywhere they have write permission, and
nothing stops them filling the volume. Then the kernel's own writes fail:
/etc/kitchen (so `secret` and `hire`), /etc/logins, the host key. Prove what
breaks when a cook fills the disk, then keep a reserve of clusters only the
headchef (and the kernel's own files) may use, the way ext filesystems keep
5% for root. Check with fsck and mtools.

## 2. One cook can fill swap — SET ASIDE 2026-10-08 (see the note at the bottom)

Swap is 16 MB, shared. Measure whether one cook's processes can take all of
it, and what happens to another cook's process that then needs a page. If
it is real, give swap the same kind of per-cook share as the process table.

## 3. A message of the day — DONE v0.55.6

/etc/motd, if it exists, shown after every login: console, pass and vault.
The headchef's to write. A small one, after the last two queues.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

- 2026-10-08, item 2: set aside, unbuilt. The measurement found that one
  cook's background processes can take all 4096 swap pages, after which
  another cook's program that needs a page is killed. No fix was written,
  and the throwaway test-program change used to measure it was reverted.
  Leave it for a person to decide whether and how to pick it up.

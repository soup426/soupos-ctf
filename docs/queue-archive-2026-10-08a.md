# Night queue

Eighth queue. The first seven are in `docs/queue-archive-2026-10-07*.md`. The
seventh's notes hold two things a person should look at: item 4 (syscalls
and unmapped buffers) set aside for a decision on how to test it, and an
open flake in the jobs segment whose next occurrence will keep its log.
This queue makes the multi-user system the last queues built usable.

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
11. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. who — DONE v0.52.0

Since v0.36.0 there can be five shells at once: the console and four SSH
sessions. Nothing lists them. `who`: each shell, its cook, where it is (the
console, or the client's address), when it started and how long it has been
idle. Test with the console and two SSH sessions as two cooks.

## 2. passwd — DONE v0.52.1 as `secret`

Secrets are PBKDF2 since v0.46.0, but the only way to set one is `hire`, and
a cook cannot change their own. `passwd`: the old secret, then the new one
twice; the headchef may set any cook's without the old one. Check that the
old secret stops working, the new one works over SSH, and /etc/kitchen holds a
fresh salt.

## 3. Home bowls — DONE v0.52.2

`hire` makes a cook who can write almost nowhere (the root is the headchef's).
Make /home/<cook> at hire, owned by the cook, and start every shell of theirs
there (console login and SSH alike). fsck and mtools as judges; existing cooks
without a home get one at their next login.

## 4. Owners in ps and orders — DONE v0.52.3

Processes have owners since v0.51.0; show them. `ps` and `orders` gain an
OWNER column, and kitchen's task table too.

## 5. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

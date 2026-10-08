# Night queue

Seventh queue. The first six are in `docs/queue-archive-2026-10-07*.md`. The
sixth's lesson is in its tests: three times a test silently skipped what it
meant to check (an unanswered prompt, an untypeable character, a file set up
where the user could not write), and each time the fix was to make the test
say so. This queue is mostly security: soupOS has a network door now, and
several rules the shell enforces were never enforced anywhere else.

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

## 1. A key logs in as its own cook — DONE v0.49.0 (the hole proven on the old kernel first)

/AUTHKEYS lines are bare public keys, and any listed key may log in as any
cook that exists, the headchef included. Give each line its cook
("headchef ssh-ed25519 AAAA... comment", the OpenSSH authorized_keys shape
with the user in front), accept a key only for the cook it names, keep
reading the old bare form as headchef-only rather than everyone, and test
that saucier's key does not open headchef's account.

## 2. Guessing a password over the network costs time — DONE v0.50.0 (124 a minute to about 2)

The vault allows six attempts per connection and a client can reconnect at
once, so the only limit on guessing is PBKDF2's 50 ms. Add a delay after each
failed attempt (doubling, capped), and a per-address count that refuses new
connections from an address with too many recent failures. Measure: attempts
per minute from one address before and after, with the real client.

## 3. Only your own processes — DONE v0.51.0 (the hole proven on the old kernel: a cook killed fbcon)

`kill`, `fg`/`plate`, `bg`/`steep` and `kill %` act on any process, so a cook
(or an SSH session) can kill another cook's programs, and `kill <id>` on a
kernel task such as the vault's worker kills it outright. Record the owner
uid in proc_t at cook time; let a cook signal only their own processes, the
headchef any; refuse kernel tasks to everyone but the headchef. Test with two
SSH sessions as two cooks.

## 4. Every syscall survives hostile pointers — SET ASIDE 2026-10-07 (see the note at the bottom)

Two syscalls trusted user pointers too far (open read its path past the first
checked byte; the fault path's copies). Audit every case in syscall_dispatch
for each pointer and length, and add evil.elf: it calls every syscall with
kernel addresses, unmapped addresses, a buffer straddling the end of user
space and absurd lengths, and must get -1 every time while the kernel logs no
panic and the shell carries on.

## 5. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

- 2026-10-07, open flake: the jobs segment's "> redirection reached the
  filesystem" (expects WCOUT lines=1 words=3 bytes=15 after `cook echo.elf
  through a file > P.TXT` and `cook wc.elf < P.TXT`) failed once under the
  full check's load and passed in 22 runs after it, 18 of them six-wide. Its
  log was not kept. Normally echo takes 0 ticks and wc reads the 15 bytes at
  once. The check was NOT loosened; failing segments now always keep their
  serial log, so the next occurrence brings its evidence.
- 2026-10-07, item 4: set aside. The audit started and found one thing
  worth a human decision: user_ok() checks that a buffer lies inside the
  user address range, not that it is mapped, so a buffer in the unmapped gap
  between a program's image and its heap would reach the kernel unvalidated.
  The planned way to test that, a program that calls every syscall with bad
  arguments, was not built. Decide how this should be tested (or whether to
  make user_ok also require each page to be mapped or demand-pageable, and
  rely on the existing tests for regressions) before picking it up.

# Night queue

Twenty-fourth queue. The first twenty-three are in `docs/queue-archive-2026-10-0*.md`.
Two items wait on a person, with notes in their archives: the seventh's
syscall-pointer item and the thirteenth's swap-shares item. Eight queues
built the shell up; this one turns back to the kernel, and asks whether the
machine holds up under long use.

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

## 1. Finished background jobs free their slots

A process that ends stays in the table until it is reaped, and the
per-cook limit (four) counts those. Find out whether a cook who runs four
background jobs that finish is then refused a fifth until someone looks at
`orders`; if so, reap finished jobs (keeping their status for `orders` to
report once) so their slots come back. Prove before fixing.

## 2. Nothing leaks over a soak

Kernel heap in use, free pages, free clusters and open VFS nodes, before
and after a soak: hundreds of program runs (pipes and redirections among
them), SSH sessions opened and closed, files written and deleted, $(...)
substitutions, scripts. Equal after, or each difference explained. Find
and fix any leak.

## 3. The open-file table runs out cleanly

A program that opens more files than the VFS table holds (16) must get -1
for the rest, not hang or corrupt anything, and the table must be whole
again once it exits. Measure, then fix whatever is wrong.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

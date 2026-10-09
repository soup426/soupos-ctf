# Night queue

Fifth queue. The first four are in `docs/queue-archive-2026-10-07*.md`. The
fourth's notes are the ones to read first: twice there the queue's own
diagnosis was wrong (the TCP waits did not burn the cpu; ATA under KVM was
PIO itself, not polling) and only measuring found it, and the worst bug of
the day (hatch leaking kernel memory) was found by setting up a measurement,
not by looking for it.

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

## 1. The disk passes fsck after everything that writes to it — DONE v0.41.0 (it did not: the permission byte was FAT's case flags)

Every filesystem test so far checks that soupOS reads back what it wrote, and
fat-longname-test has mtools read names. Nothing checks the FAT as a whole:
lost clusters, cross-linked chains, a directory entry whose size disagrees
with its chain, the two FAT copies disagreeing. `fsck.fat -n` on the host
does exactly that. Run it on the disk image after the gate's fat, vga and
soupyc segments, after fullness-test and lines-test, and after a long
fatstress, and make a clean report part of check.sh. Expect it to find
something: nothing has ever looked.

## 2. More than one SSH session at once — DONE v0.42.0 (four, a worker each; TCP table 8)

Sessions have been separate shells since v0.36.0, but the vault still serves
one connection and tells the second it is busy, because its state is one
static struct. Make the connection state per connection (a task each, up to
four, the TCP table's size), keep the busy DISCONNECT for the fifth, and test
two sessions side by side: separate cwds, separate users, Ctrl-C in one not
touching the other, both surviving a rekey.

## 3. Global memory pressure, and a better victim than FIFO — DONE v0.43.0 (pressure unreachable by arithmetic; approximate LRU, 3x fewer page-ins)

Swap evicts only at the per-process cap; when the PMM itself runs out, the
map simply fails. Evict from the faulting process when the PMM is empty too,
and replace FIFO with the clock algorithm over the accessed bit, so a page in
use stays. Measure with a working-set program: a hot set that fits plus a
cold sweep, counting page-ins of the hot set under FIFO and under clock.

## 4. Interrupt-driven DMA completion — CLOSED 2026-10-07 by measurement: polling costs ~4 port reads a request

Measured with TSC counters around the completion loop (instrumentation
reverted). A DMA request completes in 47-136 thousand cycles, about 20-60 us,
after about four polls of the bus-master status: the polling itself is a
handful of port reads. During fatstress the waits total 57 M cycles in a 30 s
run, 0.07%. During the swap greedy run they are about 24% of the run - but
inside the page-fault handler with interrupts off, where nothing else could
use the cpu unless the fault path learned to sleep, and the only gain would
be letting a second runnable process use 50 us gaps. Not built; if a
workload ever has two processes swapping at once, this is the number to beat.
Original item follows.


v0.39.0 made disk I/O DMA, which removed the per-word cost. Completion is
still polled: a few port reads per request, and a task waiting on the disk
holds the cpu. With bus-master DMA the drive's IRQ 14 says "done"; block on it.
Measure idle% during the swap greedy run and during fatstress before and after.
The fault path runs with interrupts off, so this is also the place to decide,
with a measurement, whether swap I/O should turn them on.

## 5. Programs can read directories — DONE v0.44.0 (SYS_READDIR, ls.elf, agrees with mtools)

Ring-3 programs can open, read and write files but cannot list a directory,
so `ls` can only ever be a shell builtin. A `SYS_READDIR` returning one entry
per call (name, size, is-dir), and `ls.elf` that uses it, with the output
compared against the shell's own listing of the same directory.

## 6. Memory per process, where you can see it — DONE v0.45.0

`ps` and `kitchen` show cpu but not memory. Show each process's resident
pages, its swapped-out pages and its stack growth, all of which the kernel
already counts. Check it against lazy.elf, whose numbers are known.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Carried from the third and fourth queues; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)

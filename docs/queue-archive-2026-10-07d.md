# Night queue

Fourth queue. The first three are in `docs/queue-archive-2026-10-07.md`,
`...b.md` and `...c.md`. The third one's notes are the ones to read first: it
is where the gate was split into parallel segments, where the "flaky"
job-control check turned out to be a torn log line, and where every crypto
primitive got its vectors before anything was built on it.

## Standing rules

1. **One item, one commit**, and split when an item splits naturally.
2. **`scripts/check.sh` must pass before every commit.** It builds the five
   configurations in parallel copies and runs the gate and every test script
   at once; about two minutes. `check.sh quick` is configs + gate. `-Werror`.
3. **A test script per subsystem, and the gate asserts on serial markers
   only.** Anything measured (audio, pixels, bytes on disk) lives in its own
   script that boots its own QEMU. New scripts go in check.sh's list.
4. **Verify before believing.** Twice now a "known" bug was something else:
   the job-control flake was the klog race; the 8% kitchen cost was a FAT scan.
5. **Measure the property you care about, in a capture containing nothing
   else**, and when a test fails, find out why before changing the test.
6. **Reproduce under load.** Six QEMUs at once surfaced in eight runs what
   twenty-four quiet runs never did. `scripts/jobcontrol-soak.sh` six-wide is
   the pattern for any switch-timing suspicion.
7. **Crypto gets published vectors from an independent implementation before
   it is used for anything**, and says "not constant-time, private network"
   wherever it is advertised.
8. **Kitchen names** for new shell commands: `larder`, `whistle`, `sizzle`,
   `hum`, `hush`, `hatch`, `skewer`, `sample`, `kitchen`, `pass`, `vault`.
9. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
10. **`make clean` leaves disk.img alone.** After changing anything the
    Makefile embeds in the disk, `rm disk.img && make disk`.
11. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. Swap: memory the machine does not have, kept on disk — DONE v0.35.0 (per-process cap; global pressure still fails the map)

v0.33.0 made the heap lazy and kills a process at the page cap. The honest
next step is to keep going instead of killing.

- A swap area: a fixed file on the FAT volume (`/SWAP.IMG`, 16 MB, made at
  first use) addressed by page slot, read and written through the ATA layer
  by cluster chain, so it needs no new on-disk format.
- When `map_user_page` finds the process at its cap (and, separately, when
  the PMM is empty), pick one of that process's resident heap pages, write it
  to a free slot, unmap it, and record the slot in a per-process table
  keyed by virtual page. A not-present fault on a page with a slot reads it
  back. Victim choice: oldest-mapped first is enough; the accessed bit can
  come later.
- `lazy.elf greedy` must then COMPLETE (every page written and read back
  right), slowly, and the exit line must say how many pages went out and
  came back. `kitchen` shows swap in use. The kill stays for a process past
  the swap area too: that is still memory the machine does not have.
- Care: the swap write happens inside a page-fault handler, which may be a
  ring-0 fault from a syscall holding the FAT mutex. Take the swap I/O out
  of the fault path (a request the ssh/fat task pattern can serve) or prove
  the lock order; do not discover it by deadlock.

## 2. A shell of its own for the SSH session — DONE v0.36.0 (stages a, b, c)

Today an SSH login drives THE local shell: it shares the keyboard and the
screen, and the person authenticated over SSH is not the person logged into
the shell. The pass has the same shape. Fixing it is the biggest structural
change on this list, so it is staged:

- (a) DONE v0.35.1: `shell_t` behind `cur_shell()`; the user and the
  foreground job stay global until (c).
- (b) DONE v0.35.2: `term_t`, `term_vga`, 690 calls routed, one formatter
  (`kvformat`); output proven byte-identical. The byte-stream terminal is
  built in (c), where something exercises it. Still to route for (c):
  soupyc's, the self-test's and ring-3 programs' output.
- (c) `vault` spawns a shell task on a socket-backed terminal per session,
  logged in as the authenticated user, with its own cwd and history. The
  pass can do the same or be retired. `kitchen` lists both shells.
- Per-process ownership of a terminal follows (a program's stdout goes to
  the terminal of the shell that cooked it), which is what makes `exec`
  honest: a real exit status and no echoed prompt.

## 3. A guard page under the user stack, and lazy stack growth — DONE v0.37.0

The stack is mapped eagerly at exec and overflow walks silently into
whatever is below. One unmapped page below the stack turns that into a kill
with a clear "[user] stack overflow" line, and with demand paging already in
place the stack can grow down on fault up to a limit instead of being fixed
at USTACK_PAGES. A test program that recurses until it dies.

## 4. SSH rekeying — DONE v0.38.0 (both directions, tested with RekeyLimit and `vault rekey 8`)

RFC 4253 says rekey after a gigabyte or an hour; the vault never does, and
would silently carry on with the same keys forever. Initiate KEXINIT from the
server after N packets (small for the test), run the exchange again under the
current keys, switch at NEWKEYS. The real client does this on its own too,
so the server must also ACCEPT a client-initiated rekey. Test: a session
that stays up through a forced rekey (`ssh -o RekeyLimit=` on the client).

## 5. klog from interrupt context — CLOSED 2026-10-07: no live trigger, measured; now an invariant

Audit: no interrupt handler in the tree logs. The NIC's receive path into
TCP, the mouse, the timer and the keyboard IRQ are all silent; the one
IRQ-reachable klog (an ARP drop) cannot fire for a host that has just sent us
a packet. To find out whether the race would matter if one did, a copy of
the tree was given an unprotected klog in the keyboard IRQ (on every Ctrl-C
and Ctrl-Z kill) and soaked six-wide: 192 such lines, 0 torn. The structural
reason: a ring-3 program's stderr line is written inside the int 0x80 gate,
with interrupts off, so an IRQ can only land between whole lines. What is
left is a task-context klog with interrupts on against an IRQ that logs, and
nothing logs from an IRQ. So no mechanism was built; instead "interrupt
handlers do not klog" is written down in CLAUDE.md and the preemption audit,
and the [kbd] line was not added to the real tree, since it would create the
very risk.


The task-vs-task half of the klog race is fixed (v0.31.0). An interrupt
handler's klog still lands inside a task's line. The fix is to make IRQ-side
klog ring-only, with the serial mirror drained by whoever next logs from task
context (or a service task), so the serial stream is always whole lines.
Verify with a soak: a network-heavy run (hatch under load) while a script
prints, and no torn line in the capture.

## 5b. ATA under KVM — DONE v0.39.0: not polling but PIO itself (one VM exit per word); bus-master DMA, 58x

Found measuring swap. The same greedy run that takes 296 ticks (3 s) under
the gate's emulated boot takes 5601 ticks (56 s) under KVM, nineteen times
slower: every status poll, every settle
read and every command byte is a VM exit, and a polled driver issues
hundreds per sector. String PIO (v0.34.1) only halved it. The fix is the
classic one: issue the command, block the task on a wait queue, let IRQ 14
wake it, and halt in between (so the host gets the cpu to finish the I/O).
Measure before and after with the swap greedy run under KVM and with the
free-cluster yardstick; both numbers are in the log.

## 6. The TCP waits burn the cpu — CLOSED v0.39.2: measured false (the cost is the emulated NIC); waits halt anyway

`tcp_send_on` waits for the ack in a `task_yield()` loop with no halt, and
the receive side polls. With honest cpu figures (v0.31.0) this shows: watch
`kitchen` during an `ssh` session and the ssh task reads busy while idle.
Wait on a queue the input path wakes, or at least halt between polls.

## 7. Bytes in and out, per connection — DONE v0.40.0

`kitchen`'s net section lists connections by state and peer; add bytes sent
and received (two counters in tcp_conn_t) so a stuck transfer is visible. Ten
lines, mostly in tcp.c.

## 8. Doom at the framebuffer's own resolution — DEFERRED

Carried from the third queue; the user asked to keep to OS work. Unchanged.

---

## Notes left by unattended runs

(none yet)

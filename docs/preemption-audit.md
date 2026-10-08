# What is reachable from more than one task

Written 2026-10-07, night-queue item 3. `docs/next-steps.md` has flagged
"drivers are not preemption-safe" since before processes existed; this is the
audit that closes it, file by file, with the answer written down either way.

## The question each file was asked

Since v0.8.0 several things can be running at once:

- **the shell task**, running commands;
- **one or more ring-3 processes**, each on its own task, reaching the kernel
  only through `int 0x80`;
- **interrupt handlers**: the timer, the keyboard, the ATA controller and the
  network card, each of which can land on any task's stack at any instruction.

So the question for each piece of file-scope mutable state is not "could two
things touch this in principle" but "which of those three can reach it, and by
what call path". Anything only the shell can reach is safe today, and the audit
says what would change that rather than leaving a future reader to re-derive it.

## Findings

### Fixed in this pass

**`vfs.c` — the pipe ring.** `pipe_read_op` and `pipe_write_op` both do a
read-modify-write on `p->count`, and the reader and writer are by definition
different tasks. A preemption inside either copy loop loses or duplicates
bytes. Both loops now run under `preempt_disable`; they are short and never
yield, so that is sufficient and a mutex would be overkill.

**`net.c` — the transmit staging buffers.** `txf`, the IP header buffer and the
UDP buffer are file-scope, and transmits happen from **both** task context (a
ping, a DNS query, `tcp_send`) and interrupt context (an ICMP echo reply, an
ARP reply, a TCP ACK). A task halfway through building a frame, interrupted by
the card, had its frame overwritten before the hardware saw it. The build and
hand-over now run with interrupts off: two memcpys of at most 1500 bytes, no
yielding, and an interrupt handler could not wait on a lock anyway.

### Safe today, with the reason and the trigger that would change it

**`soupyc.c`** — FIXED 2026-10-07 in v0.10.1: state now lives in a
per-invocation context hung off the task, so the trigger described below no
longer applies and `spawn()` is unblocked. The array pool remains shared, with
per-context ownership of slots. Original finding follows.

**`soupyc.c`** — 69 file-scope statics and a non-reentrant tree walker, the
scariest-looking file in the tree. It is reached from exactly two call sites,
both in `shell.c`, both the `soup` command, both on the shell task. No syscall
reaches it, `cook` runs ELF binaries rather than scripts, and `spawn()` does
not exist. **It is single-threaded today.** The trigger is night-queue item 6:
the moment `spawn()` runs a script as a background task, every one of those
statics becomes shared, which is exactly why that item lists a per-invocation
context struct as a prerequisite rather than an optional tidy-up.

**`users.c`** — login state and the permission checks. Reached only from
`shell.c`; `challenge.c` mentions `may()` in a comment but does not call it. No
syscall path consults it, because ring-3 file access goes through the VFS,
which does its permission checking in the shell before `cook` ever runs.

**`shell.c`** — the input line, history, `cwd` and the prompt bookkeeping. One
shell task, and the commands that touch them all run on it.

**`vfs.c` beyond the pipe ring** — the handle table was guarded in v0.8.5. Per-
node state (`pos`, the write buffer) belongs to one node, and a node belongs to
one process, because fd tables are per-process. The exception is a pipe, whose
two ends are deliberately held by different processes, and whose shared state
is the ring fixed above.

**`fat.c` and `ata.c`** — a recursive sleeping mutex and a lock of their own
respectively, both from the v0.7.x work. `vga.c` serialises text output.
`heap.c` and `pmm.c` take interrupts off (the latter since v0.8.5).

### Known and deliberately left

**IRQ handlers and klog** — 2026-10-07: no interrupt handler logs, and that
is now a rule (CLAUDE.md). An unprotected klog added to the keyboard IRQ in a
copy of the tree and soaked six-wide tore 0 of 192 lines: ring-3 stderr is
written with interrupts off inside the syscall gate, so the only exposure is
a task-context klog with interrupts on, against an IRQ that logs.

**`klog.c`** — FIXED for task-vs-task 2026-10-07 in v0.31.0: `klog()` and
`klog_puts()` now hold preempt_disable for the whole line, after the "cosmetic"
interleaving below turned out to be the gate's flaky job-control check (the
kernel's "continued" line torn by the process it had just resumed). An
interrupt handler's klog can still land inside a task's line. Original
finding follows.

**`klog.c`** — `ring`, `head` and `count` are written from task context and
from interrupt handlers with no guard. The indices are kept in range by the
modulo, so this cannot corrupt memory or escape the buffer; what it can do is
interleave two lines, which is a cosmetic fault in a debugging aid. Guarding it
properly would mean interrupts off around every character, including the serial
poll, which is microseconds per byte at 38400 baud and would be felt. Left
alone on purpose.

**`tcp.c`'s stack usage** — `tcp_send_seg` allocates its segment buffer on the
stack, 1044 bytes, even for a flag-only segment, and it is called from the
interrupt handler. Kernel stacks are 16 KB, so this is comfortable, but it is
worth knowing before anything else grows the IRQ path.

## How this was checked

By call graph, not by inspection of locks: for each piece of state, who calls
the functions that touch it, and can those callers be on different tasks. The
two races fixed here were found that way and confirmed to be reachable before
being fixed; the files declared safe are safe because nothing can reach them
from two tasks, not because their contents look harmless.

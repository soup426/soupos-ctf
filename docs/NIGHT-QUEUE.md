# Night queue

An ordered backlog for an unattended run, written 2026-10-06 at the end of a
long session. Each item is scoped to be finishable and gateable on its own, so
stopping between items always leaves the tree in a good state.

## Standing rules for working this list

1. **One item at a time, in order.** Finish it, gate it, commit it, push it,
   then start the next. Do not begin an item you cannot finish.
2. **The gate is the guard.** `./scripts/smoke-test.sh` must pass before any
   commit, and the three build configurations (`make`, `make DOOM=0`,
   `make CHALLENGE=1 DOOM=0`) must all build. A flaky check is a bug in the
   check: fix it rather than re-running until it passes.
3. **Do not guess at ambiguity.** If an item turns out to need a decision that
   is not written here, stop, add a note under that item saying what the
   decision is and what the options are, and move to the next item.
4. **Keep the kitchen vocabulary.** New commands get kitchen names, checked
   against `cmd_names[]` in `shell.c`, and `str_startswith` dispatch entries
   get checked against every existing name for shadowing.
5. **Prove bugs before fixing them.** This session found three latent kernel
   bugs; each one was demonstrated first and the demonstration recorded in the
   commit. Keep doing that.
6. **Stay on `main` in this worktree.** Never touch `cdctf-release`, never push
   anywhere but the Gitea remote, never publish anything to GitHub.
7. **Write the commit message for whoever reads it in a month**, including what
   did *not* work, which is usually the more useful half.

---

## 1. DHCP client — DONE (v0.9.2)

Removes the compiled-in address, which is the last thing stopping soupOS from
working on a network that is not QEMU's.

- SLIRP runs a DHCP server at 10.0.2.2, so this is **hermetic**: it needs no
  real internet and can be asserted in the gate, unlike DNS.
- Scope: DISCOVER, OFFER, REQUEST, ACK. Take the address, netmask, gateway and
  resolver out of the ACK's options. No lease renewal, no rebinding: record the
  lease time and ignore it, with a comment saying so.
- The UDP path already exists (ports 68 -> 67, broadcast to 255.255.255.255
  with a zero source address, which `ip_send` will need to tolerate).
- Shell: fold it into `plumbing` as `plumbing dhcp`, rather than a new command.
- Gate: boot with no compiled-in address and assert the log shows a lease
  being taken and the address matching 10.0.2.15.

## 2. TCP, in three separate commits — ALL DONE (v0.9.3-v0.9.5)

The real mountain. Do **not** attempt it in one pass; each stage below should
build, gate and commit on its own.

### 2a. Connection setup — DONE (v0.9.3)

- A single TCB (one connection at a time, like the resolver's single
  outstanding request). SYN, SYN/ACK, ACK, with an initial sequence number and
  the peer's window recorded.
- Retransmit the SYN twice before giving up.
- Nothing above it yet: a `knock` style test that connects and closes is enough
  to gate, asserted against the pcap and a log line.

### 2b. Data transfer — DONE (v0.9.4)

- Send and receive with sequence and acknowledgement numbers, a receive buffer,
  and a retransmit timer driven by the 100 Hz tick.
- No window scaling, no SACK, no congestion control beyond "one segment in
  flight at a time". Say so in the comments; a hobby stack that is honest about
  its limits is more useful than one that pretends.

### 2c. Close, and something to show for it — DONE (v0.9.5)

- FIN/ACK both directions, and the TIME_WAIT that follows.
- Then the payoff: `takeout <host> [path]`, an HTTP GET that prints the
  response. `takeout example.com /` is the demo.
- Gate: assert the request goes out and a 200 comes back. If that needs real
  internet, assert only what is hermetic (the connection to a local port) and
  leave the HTTP check as a manual step, the way the DNS answer is handled.

## 3. Preemption-safety debt — DONE (v0.9.6)

`docs/next-steps.md` has flagged this since before processes existed, and
processes made part of it reachable. Two sites were fixed this session (the
page allocator and the VFS handle table); these remain:

- `soupyc.c`: 69 file-scope statics and a non-reentrant tree walker. Only the
  shell runs it today, so it is not yet reachable, **but** check that claim
  before deciding it is safe, and write down the answer either way.
- `vfs.c` beyond `alloc_node`, `users.c`, `shell.c`: audit what is reachable
  from more than one task now that processes exist, guard what is, and leave a
  comment on what is not and why.
- This is an audit, not a feature: the deliverable is a short written finding
  per file plus guards where needed.

## 4. In-kernel self-test command — DONE (v0.9.7)

The gate is end-to-end only; nothing tests a subsystem directly.

- A shell command (kitchen name: `sample` is free) that runs assertions
  in-kernel and prints pass/fail per subsystem: heap alloc/free patterns, FAT
  round-trips, the VFS handle table, the pipe ring at its boundaries, the ARP
  cache, checksum vectors.
- Make it emit a single machine-readable summary line so the gate can assert on
  it with one check.


## 5. PS/2 mouse — DONE (v0.9.8 driver, v0.9.9 drag-select)

The copy-and-paste milestone said selection is keyboard-only "unless a PS/2
mouse driver is built first". This is that driver, and it is self-contained.

- The mouse is the i8042's auxiliary device on IRQ 12 (slave PIC, so remember
  `irq_unmask` opens the cascade as well). Enable the aux port (0xA8), set
  defaults, enable reporting, and read 3-byte packets: flags, dx, dy, with the
  sign and overflow bits in the flags byte.
- Packets arrive misaligned if a byte is ever dropped, so resynchronise on the
  "always 1" bit in the flags byte rather than trusting the stream.
- Keep a cursor position clamped to 80x25 and draw it by inverting the cell
  under it, the way `jot` already paints with attributes.
- Then the payoff, in a second commit: drag-select in `jot` sets the mark on
  press and the cursor on release, so the existing Ctrl+C/Ctrl+X/Ctrl+V work
  with a mouse selection unchanged.
- Gate: inject packets through the QEMU monitor (`mouse_move`, `mouse_button`)
  and assert the cursor position and a resulting selection length in the log.

## 6. soupyc's value model — DONE (v0.10.0, v0.10.1, v0.10.2)

The longest-standing limitation in the tree, flagged in `docs/next-steps.md`:
strings are a fixed `char[48]`, so the language caps every value at 47
characters, and the interpreter keeps all its state in file-scope globals so it
is not reentrant.

- Heap-backed strings first: a value struct with a pointer and a length,
  allocated from the kernel heap, freed when the slot is reused. This is the
  unlock; the 47-character cap is why a soupyc script cannot write an ELF.
- Then a per-invocation context struct, replacing the 69 file-scope statics, so
  two interpreters can run at once.
- Then `spawn(fn)` as a real background task, which is the thing the context
  work exists for. With processes and job control already in place, a spawned
  script should show up in `orders`.
- Gate: a script that builds a string longer than 47 characters and prints it,
  and a `spawn` whose output interleaves with the shell's.

## 7. The known-bugs sweep — DONE (v0.10.3, v0.10.4)

`ROADMAP.txt` carries a KNOWN BUGS / DEBT list, several items of which are
small and independently testable. Take them as one commit each:

- ~~`kfree` only forward-coalesces~~ **Already fixed; the note was stale.**
  Checked 2026-10-07 while writing item 4: heap.c walks for the predecessor and
  absorbs into it, and a self-test that frees sixteen blocks in reverse and then
  asks for one spanning them passes. `sample` guards it now. Nothing to do.
- FAT write mishandles fragmented free clusters when the disk is nearly full.
  Reproduce it first by filling a disk image, then fix.
- `shell_tab_complete` rescans the FAT on every Tab press. Cache the listing
  per directory and invalidate on write.
- VGA scrolling copies the whole buffer on every newline; it only needs to move
  24 rows, and `memmove` of 4000 bytes is not where the time should go.
- ~~No bounds check on `prompt_row`~~ **Stale, checked 2026-10-07.**
  `vga_set_cursor_impl` clamps row and col, and the multi-match completion path
  calls `prompt()`, which re-records `prompt_row` after the candidate list
  scrolls. NEW and separate: `spawn()` means arbitrary output can now land
  while the line editor is active, which can desync `prompt_row` the way
  background job reports were handled to avoid. Needs a scroll counter in
  vga.c that the line editor subtracts from `prompt_row`, and a screendump to
  verify. Not guessed at yet.

**2026-10-07, item 7: three of four done, v0.10.3, 60 checks green.** The VGA
entry named a remedy that measured 47% worse (memmove is a byte loop); words
are the answer, 504 -> 271 ticks. The FAT entry was mis-described: fragmentation
is fine, the real bug was a cluster leak on every write failure path, now fixed,
with `larder` added because free space was previously unobservable. Left: the
tab-complete cache (measure first), and the empirical nearly-full repro of the
leak, which `larder` now makes cheap - fill the disk from a script, delete the
files, and free space must return to its starting value.

**2026-10-07, item 7 CLOSED, v0.10.4, 61 checks green.** The leak is now
demonstrated by scripts/fullness-test.sh (22 KB free -> 0 KB with the leak,
unchanged with the fix), and that script was checked in reverse by putting the
leak back. Tab completion is closed as won't-do with numbers: 1.9 ms per scan
normally, 5.5 ms on a 128-entry directory, both below perception, and a cache's
invalidation surface got worse when spawn() landed.

### New item, found while writing the fill script — DONE (v0.10.5)

`let` inside a loop body creates a fresh binding per iteration and MAX_VARS is
64, so any loop declaring a variable dies after 64 passes. Scopes are popped on
function return, not at block exit. The fix is to record nvar at block entry in
eval_block and restore it on the way out, which is a few lines, but it changes
the language's scoping rules, so it wants its own commit and a test that a
variable declared in a loop is not visible after it.

## 8. Sound — DONE (v0.11.0 driver, v0.11.1 Doom effects, v0.13.0 mixing)

The PCI groundwork from the network card (BARs, bus mastering, interrupt
lines) is exactly what a sound card needs, so this is cheaper now than it has
ever been.

- An AC97 controller (QEMU: `-device AC97`) is the easier target: two BARs,
  a buffer descriptor list of up to 32 entries, and an interrupt per completed
  buffer. Set the sample rate, build a BDL pointing at a ring of PCM buffers,
  and feed it.
- First milestone is a sine wave at a known frequency, verified by dumping the
  samples QEMU writes (`-audiodev wav`) and checking the wave's period on the
  host rather than by ear.
**2026-10-07, milestone 1: done, v0.11.0, 64 checks green.** The driver, the
`whistle` command, and a host-side frequency check in the gate. The queue's
insistence on measuring rather than listening paid for itself immediately: a
phase-step error of 256x played a 1.7 Hz wave at full amplitude, which is
silence to the ear and obvious in the capture. Now 440.00 Hz over 172 periods.

- Second milestone is the obvious one: Doom's sound effects. The WAD's `DS*`
  lumps are 11025 Hz unsigned 8-bit with a short header, which needs resampling
  to whatever rate the card is set to. Mixing more than one effect at a time is
  a third milestone, not part of the second.

## 9. A real framebuffer — DONE (v0.12.0 detection, v0.12.1 console)

soupOS is 80x25 text plus 320x200 mode 13h, both of which are as old as the
hardware they imitate. GRUB can hand us a linear framebuffer at boot through
the multiboot header, with no real-mode BIOS calls needed from protected mode.

- Request a framebuffer in `boot.asm`'s multiboot header, read the address,
  pitch, width, height and bit depth out of the multiboot info the bootloader
  already passes to `kernel_main`, and map it.
- Then a console on top of it: an 8x16 bitmap font (there is already one being
  saved and restored around mode 13h in `vga13h.c`), a glyph blitter, and the
  same `vga_*` interface so nothing above has to change.
- Keep the text-mode path working and selectable, because the gate drives the
  shell through it and mode 13h is what Doom uses.
- This is the item most likely to sprawl. If it starts touching Doom or the
  gate's screen handling, stop and write a note.

**2026-10-07, stage 1: done, v0.12.0, 66 checks green on the text image.**
Detection, mapping and drawing, verified pixel-exactly by screendump
(scripts/framebuffer-test.sh). The item DID touch the gate, exactly as warned:
the multiboot header's video request overrides gfxpayload, so the first
attempt put the ordinary text boot into graphics mode, caught by the VGA
scroll self-test failing. Resolution: FB=1 build flag, separate iso-fb image,
reasons written in boot.asm. Stage 2 (the 8x16 font console behind the vga_*
interface) is still open, and the sprawl warning stands: it means touching
every vga_* call path, so it wants a tick of its own.

---

## Choosing what to do

The items are independent except where stated (2a before 2b before 2c). If one
turns out to be blocked, badly scoped, or to need a decision that is not
written down, leave a note in the section below and move to the next item
rather than improvising a different plan.

---

## Notes left by unattended runs

Append here rather than editing items above: what was finished, what was
stopped on and why, and anything that needs a human decision.

**2026-10-07, item 1 (DHCP): done, v0.9.2, 42 checks green.** No decisions
needed. One thing future items should know: SLIRP replies fast enough that a
UDP response can be handled before the send call returns, so arm the receive
port with `net_udp_listen` *before* sending anything that expects an answer.
TCP (item 2) will hit the same timing.

**2026-10-07, item 2a (TCP connection setup): done, v0.9.3, 43 checks green.**
The gate now starts a listener on the host's loopback and connects to it
through 10.0.2.2, so it stays hermetic. One thing left unexplained on purpose:
QEMU ignores the first SYN after an ARP exchange and answers the retransmit
7 ms later, so the first connect costs a second. Both SYNs are byte-identical
on the wire with the same destination MAC and checksum, so what soupOS emits is
correct; the behaviour is QEMU's. Not worth more time, but if stage 2b sees
odd first-packet behaviour, this is the context.

**2026-10-07, item 2b (TCP data): done, v0.9.4, 45 checks green.** Stop-and-wait
as scoped. The gate's host listener now echoes upper-cased, so the data path is
checked both directions. No decisions needed. Stage 2c inherits a FIN that is
already acknowledged and flagged (`tcb.peer_fin`), so the close work is the
state machine, not the detection.

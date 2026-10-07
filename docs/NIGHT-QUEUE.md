# Night queue

Third queue. The first (nine items) is in `docs/queue-archive-2026-10-07.md`,
the second (eight) in `docs/queue-archive-2026-10-07b.md`. Both notes sections
are worth reading before starting here: most of what they record is about how
to measure this machine, which is the part that keeps being the hard bit.

## Standing rules

1. **One item, one commit**, and split when an item splits naturally.
2. **The gate must pass before every commit, and the configuration check must
   GATE the commit, not merely precede it.** Five configurations: default,
   `FB=0`, `DOOM=0`, `CHALLENGE=1 DOOM=0`, `PROFILE=1`. `-Werror` is on.
3. **Run the test scripts an item could break.** `framebuffer`, `doom-fb`,
   `mixer`, `music`, `sound`, `hatch`, `fullness`, `console`, and
   `doom-profile` for anything performance-shaped.
4. **Verify before believing.** Four of five entries on the first queue's
   known-bugs list were stale or mis-described.
5. **Measure the property you care about, in a capture containing nothing
   else.** Duration could not tell a mixed sound from a dropped one; a
   contiguous audio capture made a 513 ms effect read as 853; a span that
   included a later sound read 4251 ms.
6. **When a test fails, find out why before changing the test.** The mixer's
   correlation fell to 0.9384 and the answer was a 63 ms ring latency, not a
   bad threshold. Lowering the number would have hidden a real property.
7. **Kitchen names** for new shell commands: `larder`, `whistle`, `sizzle`,
   `hum`, `hush`, `hatch`, `skewer`, `sample`.
8. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
9. If an item is blocked or needs a decision that is not written down, leave a
   note at the bottom and move on.

---

## 1. The flaky job-control check — INVESTIGATED (v0.19.1), still open

One gate run failed on `stopped job kept running: 1 lines while parked`; the
next passed, and nothing in that commit touched job control.

- The check asserts a Ctrl+Z'd process prints nothing while parked. One line
  escaping suggests the stop is taken at a safe point that arrives *after* the
  process has already queued a write, which would make it a real (if harmless)
  latency rather than a test artefact.
- Find out which. Instrument the stop path, run the check in a loop until it
  fails, and look at where the escaping line came from.
- **Do not loosen the check until that is known.** A gate that is adjusted to
  accept what it caught stops being worth running.

**2026-10-07: not reproduced in 24 runs, so this stays open.** Ten in isolation
and fourteen under the gate's conditions. Below about 4%. The check is
unchanged; its failure now prints the surrounding log lines, and
scripts/jobcontrol-soak.sh drives the sequence in bulk. Worth knowing before
the next attempt: proc_take_stop logs "stopped" when it parks, not when the
stop is requested, so stop latency alone does not explain a line landing after
it. A second anomaly appeared once in fourteen - the job never logged
"continued" - which may or may not be the same bug seen from the other side.

## 2. Doom at the framebuffer's own resolution — DEFERRED (user asked to keep to OS work)

Doom renders 320x200 and the scaler blows it up 3x. The framebuffer is
1024x768 and `doom-profile` says there is 33x headroom at the 50 fps cap, so
the pixels are available.

- Render at 640x400 natively (a 2x of the original geometry, which keeps the
  aspect and the fixed-point maths honest) and blit 1:1 centred, or at 960x600
  if the renderer's fixed-point survives it.
- `VGA13_W`/`VGA13_H` are compile-time and spread through doom.c, so expect the
  same shape of work as the console resize: generalise first, change second.
- Verify with `doom-profile` (the frame cost will rise; check it still clears
  the cap) and with a screendump compared against the 3x-scaled version for
  the same scene - the picture should be the same picture, more finely drawn.

**2026-10-07: set aside at the user's request, to keep this queue on the
operating system rather than the game.** The survey is worth keeping for
whenever it is picked up: 41 references to VGA13_W/H in doom.c, four
width-sized scratch arrays, two full-screen buffers, and - the real one -
`PROJ_DIST 160`, which is the projection distance for a 90 degree field of
view and is therefore VGA13_W/2. Doom's internal resolution also has to become
independent of mode 13h's, because the hardware mode cannot be 640x400, so the
buffers want sizing by a maximum with the live dimensions as runtime values.
Same shape as the console resize: generalise in its own commit, then change.

## 3. Writing long filenames — DONE (v0.20.0)

Reading them landed in v0.16.0; a file created with a long name still gets a
mangled 8.3 name, which is an asymmetry that will bite someone.

- Generating the 8.3 alias is the fiddly half: strip spaces and dots, upper
  case, and a `~N` tail that has to be unique in the directory.
- The LFN chain is written in reverse with the checksum of the alias in every
  entry, so `fat_write` must allocate the right number of directory slots
  before it starts.
- Verify by writing a long name from soupyc, then reading the image with
  `mdir` on the host: mtools is an independent implementation, so it agreeing
  is real evidence.

**2026-10-07: done, v0.20.0, gate 72.** mtools reads both written names with
distinct aliases. The consecutive-slot requirement was the part the item did not
mention and the old allocator could not meet. Noted while there: soupOS's
permission byte is VFAT's case-flags byte, so other tools display our aliases in
lower case - cosmetic, pre-existing, now documented.

## 4. More than one connection at a time — DONE (v0.21.0)

`hatch` serves one request, then listens again, because the listener IS the
connection - one block of state.

- A small table of connections (four is plenty), with the listener separate
  from the accepted ones, so a SYN arriving mid-request is queued rather than
  refused.
- `tcp_input`'s four-tuple match becomes a lookup. That is the whole change;
  the state machine itself does not move.
- Verify with two `curl`s in parallel against `hatch`, both getting their own
  file intact.

**2026-10-07: done, v0.21.0, gate 72.** Three in parallel, each with its real
size, across three different slots - the slot numbers being what proves they
overlapped rather than queued. The item's prediction held exactly: the lookup
was the whole change and the state machine did not move. Lesson for the next
script: a bare `wait` also waits for QEMU, which never exits, and that looked
like a slow server until the fetches were timed at 4-21 ms each.

---

## SSH, in stages (asked for 2026-10-07; takes priority over 5-8 below)

The pass (v0.22.0) gives clear-text access today. SSH replaces it with the
real thing. Every stage is its own commit, and every primitive is checked
against PUBLISHED test vectors before anything is built on it - a hand-rolled
cipher that "looks right" is the one thing worse than no cipher, because it
convinces people to trust it.

Two honest notes up front. This is crypto written from scratch in a hobby
kernel: it is for learning and for a private network, and it should say so
wherever it is advertised. And soupOS has no entropy source worth the name,
which matters more than any of the ciphers, so that comes early.

### S1. SHA-256 and HMAC-SHA256  — DONE v0.23.0
FIPS 180-4 vectors for the hash ("abc", the 448-bit and million-'a' cases),
RFC 4231 vectors for HMAC. `sample` runs them.

### S2. Entropy
A `random.c` that mixes RDRAND when CPUID says it exists, timer and keyboard
timing jitter, and the RTC, through SHA-256 into a pool. The check is not a
test vector - there is none - but that two boots produce different output and
that a megabyte of it passes a simple monobit and runs test.

### S3. ChaCha20 and Poly1305
RFC 8439 vectors, including the AEAD construction (section 2.8.2), because
SSH's chacha20-poly1305@openssh.com uses it in a particular way.

### S4. Curve25519
RFC 7748 vectors: the two scalar-multiplication cases and the iterated one
(1, 1000 iterations; the million is optional). Needs 255-bit field
arithmetic in 32-bit limbs, which is the bulk of the work and where a bug
hides silently - the iterated vector is what catches it.

### S5. Ed25519
RFC 8032 vectors, for the host key. Shares the field arithmetic with S4.
Generate the host key once at first boot with S2 and keep it on the FAT
volume, so the host fingerprint is stable across reboots.

### S6. SSH transport and key exchange
Version string exchange, the binary packet protocol, and
curve25519-sha256 kex with an ssh-ed25519 host key. Verify by connecting
with the real `ssh` client with -vvv: it must reach "SSH2_MSG_NEWKEYS sent"
and report the expected fingerprint, even before auth exists.

### S7. Userauth and the session channel
Password auth against users.c; then a session channel with pty-req and shell,
attached to the same console sink and byte translator the pass uses. The
acceptance test is `ssh headchef@localhost -p <port>` from the host giving a
working shell, with `pass` then retired or left disabled by default.

## 5. A live view of the machine

`ps` has CPU% now but it is a snapshot you have to keep re-typing.

- A `kitchen` command (kitchen-appropriate: it is the whole room) that redraws
  tasks, their CPU share, memory and the disk's free space once a second until
  a key is pressed. The console is 128x48 now, so there is room to show it
  properly.
- The thing to be careful about: the renderer must not become a task that
  spends its time redrawing itself. Check its own CPU share in its own output.

## 6. The klog ring's race

`docs/preemption-audit.md` records this as knowingly left: `ring`, `head` and
`count` are written from task and interrupt context with no guard, so two
lines can interleave. It cannot corrupt memory, only the log.

- The audit's reason for leaving it was that guarding it meant interrupts off
  around a serial write at 38400 baud. A per-line staging buffer with one
  guarded commit would avoid that entirely.
- Verify by logging hard from a task while an interrupt handler logs too, and
  checking every line in the ring is whole.

## 7. soupyc writes files properly

The language can create, read and now delete, but a script still cannot do the
one thing scripts are for: produce a file from another file.

- A `lines(path)` returning an array, and a `write_lines(path, array)`, both of
  which the heap-backed strings and arrays already support.
- Then the obvious test: a script that reads a file, transforms it and writes
  it back, verified byte for byte against the same transformation on the host.

## 8. Memory the machine does not have

The PMM is a bitmap over physical pages and every process gets its own page
directory, which is most of the machinery for demand paging already.

- Start with the honest small version: a page fault handler that distinguishes
  "not mapped and never will be" (kill the process) from "not mapped yet"
  (allocate and continue), and grow the user heap lazily instead of up front.
- Swapping to disk is the item after that, not part of this one.
- Verify with a program that touches far more memory than it allocates up
  front, and with `larder`/`memory` showing the pages arriving as it runs.

---

## Notes left by unattended runs

Append here rather than editing the items above.

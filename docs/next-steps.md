# soupOS: what's left

Written 2026-08-26, after the CTF chain landed. Split by what actually forces
the work: the event, keeping the project safe, and where soupOS goes next.

---

## 1. Do these regardless

### ~~The deliberately planted vulnerabilities are in mainline~~ DONE 2026-08-26

Fixed in `07c863b`. Both bugs now sit behind `CHALLENGE=1` (default 0), and
`challenge.c` compiles out entirely. Verified both directions: the default
build refuses both exploits and contains no flag strings; the `CHALLENGE=1
DOOM=0` build still captures all four flags. Original note kept below for
context.

### Original: the planted vulnerabilities were unconditional

This is the important one. Two bugs were planted for the CTF and **neither is
behind a build flag**:

- `soupyc.c` array assignment bounds-checks only the upper end, so a script
  can write below the pool. soupyc runs in ring 0, so that is a kernel write
  from a text file.
- `exec_elf` deliberately does not validate `p_offset`, so any ELF can read
  arbitrary kernel memory.

Doom is gated (`DOOM=0`). These are not. So every future soupOS build carries
a script-to-ring-0 write and a kernel memory disclosure, and anyone who runs
the OS for anything else inherits them.

Fix the same way Doom was handled: a `CHALLENGE` make variable, default 0,
that restores the real bounds check and validates `p_offset` unless building
the challenge image. `challenge.c` should compile out with it.

Until that lands, **treat every soupOS build as the challenge build**, and do
not use it for anything where the ring-3 boundary is supposed to mean
something.

### 19 commits exist on one laptop

There is no git remote. All of it — the OS, the challenge, the docs, the solve
guide — lives in one directory on `soup`.

Gitea is up and reachable on `10.0.0.160:3000`. Pushing there is a few
minutes' work and removes the single largest risk to the project.

Two notes when it goes up: the repo contains the CTF flags and headchef's
secret in `src/challenge.c` and `src/users.c`, so it wants to be private until
after the event, and `docs/challenge-solutions.md` is a full spoiler.

---

## 2. Before the event

### ~~Solve the chain from a browser, as a player~~ DONE 2026-08-26

Driven entirely through the PS/2 keyboard via QEMU `sendkey`, which is the
same input path noVNC uses, with screendumps at each step. All four stages
captured their flags.

The specific worry is resolved: `jot` renders correctly and takes input this
way. Screendumps show the editor with typed text, the `~` empty-line markers,
and its status bar (`L.HEX *  Ln 6,C 1  ^S save ESC quit`). Typing the 300
hex characters for stage 3 through it took about 11 seconds and produced a
working 150-byte ELF.

One cosmetic note: stage 3 dumps 0x40 bytes, and the flag is 39 characters, so
about 24 bytes of binary padding print after it and shove the exit message
around. The flag is plainly readable; it just is not tidy. Shrinking the
window to 0x30 would clean it up.

Original note below.

### Original: solve the chain from a browser

Everything so far was verified over the **serial console**, plus a screendump
proving the deployed container boots. The chain has not been solved through
noVNC the way players will actually do it.

That gap is not cosmetic. `jot` writes straight to `0xB8000` and does not
render over serial at all, and `jot` is the delivery mechanism for stages 3
and 4, because the hex is longer than the 256-byte shell line. So the exact
step players depend on most is the one least verified in the medium they will
use. Drive it by hand once, end to end.

### Decide the handout

Stages 3 and 4 need `nm kernel.elf` for symbol addresses and the source to
find the bugs at all. The design docs recommend shipping source plus the
vulnerable diff. Nothing has actually been packaged yet.

### Housekeeping

- `soupos-web` (retired placeholder) removed from the deployer 2026-08-26.
- Doom now ships in the challenge image (2026-08-27), so `soupos-doom` is
  redundant and the earlier "keep Doom out of the CTF build" advice is
  superseded.
- Instance timeout is set to 7200s (2h). Confirm that suits the event length.
- `crash.elf` ships on the challenge disk. It is a debugging aid, harmless,
  but decide whether it stays.
- Flags are baked into the image, so rotating them means a rebuild and
  redeploy. Fine as designed, worth knowing before the morning of.

---

## 3. Where soupOS is limited as a platform

Ordered by how much else they unblock.

### ~~Per-process address spaces~~ DONE 2026-08-27 (`86c9060`)

Each program gets its own page directory, recorded on the task so the
scheduler restores CR3 when the process is resumed.

### ~~Concurrent processes~~ DONE 2026-10-06 (v0.8.0)

A program is a process now: its own task, its own slot in an 8-entry process
table, its own ring-0 stack to trap onto. `cook prog &` backgrounds, `jobs`
lists, `kill`/`kill %`/Ctrl-C end a program, and the exit code is reaped by
the parent. Two things that looked like details turned out to be the whole
job: `tss.esp0` was global (and `tss_set_esp0()` was never called), and so was
`kernel_resume_esp`.

Still open from the original plan here: `fork`/`exec` and pipes, then `fg`/`bg`
and process groups. See
`docs/superpowers/specs/2026-10-06-concurrent-processes-design.md`.

### ~~Drivers are not preemption-safe~~ AUDITED AND CLOSED 2026-10-07

See `docs/preemption-audit.md`: two real races found and fixed (the pipe ring
and the network transmit buffers), the rest answered per file with the reason
recorded. Original note below.

### Drivers are not preemption-safe (original note, partly fixed earlier)

v0.7.4 armed timer preemption but only `heap.c` was guarded. `fat.c` now takes
a sleeping recursive mutex, `ata.c` takes its own, and `vga.c` serialises text
output; the smoke gate covers all three. **Still unguarded:** `soupyc.c` (69
file-scope statics, and the interpreter is not reentrant), plus `vfs`, `users`
and `shell`.

This is still why Doom refuses to start while another task is alive.

### ~~`sched_locked` is one global, not per-task~~ DONE (`ead085c`)

`preempt_disable()` keeps its depth in `task_t`, so it travels with the task
that took it. `proc_spawn`, `proc_wait`, `proc_exit` and `mutex_lock` all rely
on that now.

### soupyc's value model

Strings are a fixed NUL-terminated `char[48]`, arrays are a static pool of 24,
and the interpreter keeps all its state in file-scope globals so it is not
reentrant. Consequences: the 47-character cap shapes what the language can do
(it is why every CTF flag had to be short, and why a script cannot write an
ELF), and `spawn()` is impossible without a per-invocation context struct.

Heap-backed strings are the unlock.

### Test coverage (better, still end-to-end only)

The gate is 22 checks now, including concurrency, both kill paths, Ctrl-C, and
an address-space leak check that compares free page counts across two runs of
one program. It is still entirely end-to-end: there is no per-subsystem unit
test, and the in-kernel `selftest` command below is still worth building.

Original note: two scripts, both end-to-end smoke tests. They are genuinely good — they
caught the credential change breaking every ring-3 check — but there is
nothing per-subsystem. FAT, the VFS, the heap, the scheduler and soupyc have
no direct tests, and the audit that found five real bugs was manual.

Worth adding a shell command that runs in-kernel unit tests, so a single boot
can assert against subsystems rather than scraping serial.

### ~~No CI~~ DONE (`4d3c114`)

A workflow builds both configurations and runs every gate on push.

Original note: every build, gate run and deploy in this project has been by hand. Given the
repo is not even pushed anywhere yet, this is downstream of section 1, but a
runner that does `make && make DOOM=0 && both gates` on push would have caught
the credential breakage immediately.

### Smaller, already tracked

`ROADMAP.txt` lists 53 open items. The ones most likely to bite:

- `kfree` only forward-coalesces, so long sessions fragment the heap
- FAT write mishandles fragmented free clusters when the disk is nearly full
- `shell_tab_complete` rescans the FAT on every Tab press
- No bounds check on `prompt_row` when the screen is full
- No networking at all (RTL8139 is speculative in the roadmap)

---

## Suggested order

1. Push to Gitea. Minutes, removes the largest risk.
2. Gate the CTF vulnerabilities behind `CHALLENGE`. Until this is done there
   is no clean soupOS to build on.
3. Solve the chain through a browser once.
4. Package the handout.
5. Then platform work, starting with per-task `sched_locked`, then driver
   safety, then per-process address spaces.

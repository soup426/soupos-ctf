# soupOS challenge chain: build plan

**Decision (2026-08-26):** ship one four-stage chained challenge.

1. Mise en Place
2. Salt to Taste
3. Bad Recipe
4. Too Many Cooks

Designs are in [`challenge-ideas.md`](challenge-ideas.md). This is the plan to
build them.

---

## One thing had to be resolved first

As originally written, **Bad Recipe and Too Many Cooks both end at "arbitrary
kernel write"**. Chained in that order, stage 3 already wins and stage 4 is
decoration. A chain only works if each stage yields a capability the next one
actually needs.

So the primitives are split. This is the spine of the whole plan:

| Stage | Capability gained | Flag lives in |
|---|---|---|
| 1. Mise en Place | Read a file the permission bits forbid | A file, mode 600, owned by headchef |
| 2. Salt to Taste | Become uid 0 | Printed by a uid-0-gated command, **not a file** |
| 3. Bad Recipe | **Read** arbitrary kernel memory | A string in kernel memory, never on disk |
| 4. Too Many Cooks | **Write** kernel memory / hijack control flow | Emitted only by a routine nothing calls |

Read a file, then become root, then read kernel memory, then control kernel
execution. Each rung is strictly stronger than the last and cannot be skipped.

Two consequences fall out of that table, and both are load-bearing:

- **Stage 2's flag cannot be a file.** The stage-1 bypass reads any file, so a
  FLAG2 file would be readable at stage 1 and the chain collapses.
- **Bad Recipe must be a read primitive, not a write.** That means constraining
  the half of the loader bug that writes (see stage 3).

---

## What already exists vs what has to be built

Checked in the tree, not assumed.

| Piece | State |
|---|---|
| Stage 1 bug (soupyc `open()` skips `may()`) | **Already real.** `soupyc.c` calls `vfs_open()` directly. |
| Stage 2 bug (32-bit unsalted hash) | **Already real.** `alphasoup_hash`, `/etc/kitchen`, `chef` checks hash equality. |
| Stage 3 bug (loader trusts the file) | **Already real.** `usermode.c:110` does `memcpy((void *)ph->p_vaddr, buf + ph->p_offset, ph->p_filesz)`. Neither `p_vaddr` nor `p_offset` is validated. |
| Stage 4 bug (negative array index) | **Must be planted.** `soupyc.c:1141` currently checks `if (i < 0 \|\| i >= a->len)`. Correct today. |

Three of the four vulnerabilities already exist. The bulk of the work is
scaffolding, lockdown, and verification, not writing bugs.

---

## Work items

### A. Accounts and starting state

- [ ] Seed a `cook` account in `/etc/kitchen` at image build time. Today
      `users_init()` seeds only `headchef`. Add a second roster entry with a
      non-zero uid.
- [ ] Decide and document the cook's password; it goes in the CTFd challenge
      description. Players start knowing it.
- [ ] Confirm a cook can log in at the boot prompt and lands in a working shell.
- [ ] Keep headchef's password something a 32-bit brute force finds, but not
      guessable. It must **not** be `soup`, or stage 2 is free.

### B. Flags

- [ ] **FLAG1**: file on disk, owner headchef, mode 600. `pour` must refuse it
      for a cook.
- [ ] **FLAG2**: not a file. Add a shell command that prints it only when
      `users_is_headchef()`.
- [ ] **FLAG3**: a string in kernel memory, placed on the kernel heap near the
      buffer `exec_elf` reads the ELF into, so the out-of-bounds read reaches
      it. Position deliberately; do not leave it to luck.
- [ ] **FLAG4**: emitted only by a function that is never called on any normal
      path. Stage 4 is reaching it.
- [ ] All four go in the CTFd challenge as static flags (baked image, one value
      per instance, per the earlier decision).

### C. Lockdown, so the intended path is the only path

- [ ] **Remove `taste` from the challenge build.** Gating it behind uid 0 is not
      enough: stage 2 *makes* the player uid 0, and an arbitrary kernel read at
      that point hands them stage 3 for free. It has to be gone.
- [ ] Audit every other command that touches raw memory or prints kernel
      addresses. `dmesg` and `spill` are fine (`spill`'s panic dump is a useful
      leak aid for stage 4 and does not disclose a flag), but confirm rather
      than assume.
- [ ] Verify no path prints FLAG3/FLAG4 by accident: `dmesg`, the panic dump,
      `ladle`, `broth`.
- [ ] Leave the soupyc `open()` bypass in place. It is stage 1.

### D. Stage 3: split the loader bug

The loader currently trusts both where it writes and where it reads from. Stage
3 needs the read half only.

- [ ] **Add** validation that `p_vaddr` and `p_vaddr + p_memsz` fall inside the
      user range, so a PT_LOAD cannot target kernel memory. This closes the
      arbitrary write that would otherwise skip stage 4.
- [ ] **Leave** `p_offset` / `p_filesz` unvalidated against the actual file
      size. The loader then copies from past the end of the heap buffer into
      the program's own image, and the program can print what it got.
- [ ] Place FLAG3 on the heap so it lands within reach of that over-read.
- [ ] Write the reference exploit and confirm it recovers FLAG3 reliably, not
      just once.

The resulting bug reads as a plausible mistake: *the loader checks where it
writes, but not where it reads from.*

### E. Stage 4: plant the soupyc bug

- [ ] Remove `i < 0 ||` from the bounds check at `soupyc.c:1141` so `a[-N] = x`
      writes below the array pool.
- [ ] Confirm the read path stays bounded, so the write is the only primitive
      and stage 4 is distinct from stage 3.
- [ ] Add the never-called routine that prints FLAG4.
- [ ] Write the reference exploit: compute the offset from the pool to a
      target, overwrite it, reach the routine.
- [ ] Sanity-check reachability. The pool is a file-scope `arrays[24]`, so what
      sits below it is fixed at link time and the offset is stable across
      boots. Verify that, because the whole stage depends on it.

### F. Getting an exploit binary onto the disk

**This is the one genuinely blocking piece of scaffolding, and it is easy to
underestimate.**

soupOS has no upload. Stage 3 needs the player to run a crafted ELF, and the
obvious route does not work: soupyc strings are a fixed `char[48]`,
NUL-terminated, so `write(f, chr(0))` cannot express a NUL byte. ELF headers
are full of NUL bytes. **A player cannot write an ELF from soupyc.**

Plan: ship a hex-to-binary stager.

- [ ] Build `unhex.elf` with the normal toolchain and put it on the image. It
      reads a hex text file via `SYS_OPEN`/`SYS_READ` and writes the decoded
      bytes with `SYS_WRITE`, which takes an explicit length and so handles
      NULs fine.
- [ ] Player workflow: write hex with `jot` (or generate it with a soupyc
      script), run `cook unhex.elf HEX.TXT OUT.ELF`, then `cook OUT.ELF`.
- [ ] Document this in the challenge description. It is scaffolding, not a
      puzzle, and hiding it just burns players' time on the wrong problem.
- [ ] Verify `SYS_WRITE` to a VFS file (fd >= 3) actually persists. The v0.7.1
      notes flag streaming FAT writes as unfinished; write-mode files buffer in
      the heap and flush on close, so this should work, but it must be proven
      before the chain depends on it.

### G. Exploit output must go to the screen

Players see the VM through noVNC, which is the VGA framebuffer. In the
challenge container COM1 goes to a log file inside the container that players
cannot read.

- [ ] Make sure the reference exploits print with `SYS_WRITE(fd=1)` (screen),
      never `fd=2` (serial).
- [ ] Say so in the challenge description. A player debugging into fd 2 will
      see nothing and conclude their exploit failed.

### H. Packaging and deployment

- [ ] Rebuild the image with the challenge build (cook account, flags, `taste`
      removed, stage 3 and 4 bugs in place).
- [ ] Decide the handout. Recommendation stands: full source plus the
      vulnerable diff. Reversing a bespoke 32-bit kernel *and* building four
      exploits is what tips this from hard into unsolvable.
- [ ] Rebuild `soupos-web` on ctfd-test and update CTFd challenge 7 (or split
      into four CTFd challenges, one per stage, so partial progress scores).
- [ ] Follow `~/Projects/ctfd-ployer-stack/docs/adding-a-challenge.md`.

### I. Verification

- [ ] Write a working reference exploit for **all four** stages. A challenge
      nobody has solved end to end is not a challenge, it is a guess.
- [ ] Run the full chain start to finish on a fresh instance, through the
      browser, as a player would.
- [ ] Confirm each stage is *necessary*: try to reach FLAG4 without doing
      stages 1 to 3 and fail.
- [ ] Confirm each stage is *sufficient*: nothing else is needed beyond the
      handout.
- [ ] Time a solve. If stage 3 or 4 takes more than a couple of hours with the
      source in hand, the difficulty is wrong.
- [ ] Extend `scripts/smoke-test.sh` to cover the challenge build so a later
      change cannot silently break the chain mid-event.

---

## Risks

**The chain is only as good as its weakest gate.** Stage 2's flag not being a
file, and `taste` being removed, are the two things holding the ladder
together. Both are easy to undo by accident.

**Stage 4's offset must be stable.** If the layout below the array pool shifts
between builds, the reference exploit breaks and so does every player's. Pin
it, and re-verify after any change to `soupyc.c`.

**The stager is a dependency, not a detail.** If `SYS_WRITE` to a file turns
out not to persist, stages 3 and 4 have no delivery path and the chain stops
at stage 2. Verify item F early, before building anything on top of it.

**Four stages is a lot of exploit development.** Three bugs already exist,
which helps, but writing four reference exploits and proving each stage is
both necessary and sufficient is the bulk of the remaining work.

---

## Suggested order

1. **F** (prove the stager works) and **A** (accounts). Everything depends on these.
2. **B** and **C** (flags, lockdown). Establishes the ladder.
3. **Stage 1 and 2** end to end, with reference solves. Fast, the bugs exist.
4. **D** (split the loader bug) and stage 3's reference exploit.
5. **E** (plant the soupyc bug) and stage 4's reference exploit.
6. **H** and **I** (package, deploy, verify the whole chain).

Front-loading F is deliberate. It is the item most likely to force a redesign,
and the cheapest to check.

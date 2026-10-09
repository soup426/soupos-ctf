# soupOS CTF challenge designs

Target: the hardest challenge at a college-level CTF. Very difficult, still
solvable. Written 2026-08-25 after auditing the tree, so every "bug that
already exists" below was read in the source or observed running, not guessed.

---

## Close these two first, or nothing below matters

Both are real, both are in the tree today, and either one solves every
challenge here in about three lines.

### 1. soupyc's `open()` ignores permissions entirely

`src/soupyc.c` calls `vfs_open()` directly. The shell's `may()` gate, the thing
that enforces the owner/rwx bytes on `pour`, `stir`, `cd` and friends, never
runs on this path. So a low-privilege `cook` does this:

```
let f = open("/FLAG.TXT")
pour read(f, 40)
close(f)
```

Two file paths, one check. Unless this *is* your intended stage 1 (it makes a
good one, see Challenge A), it is a universal bypass.

### 2. `taste <addr> [len]` is an ungated arbitrary kernel read

Any user, any address, hex + ASCII, up to 512 bytes a call. The kernel is
identity-mapped, so this is a complete kernel memory disclosure primitive
handed to the player at the prompt before they do anything. It trivialises the
leak stage of every pwn challenge below.

Fix both by gating on `users_is_headchef()`, or drop `taste` from the shipped
build. If you want players to *have* a read primitive, give them a
deliberately flawed one they must find, rather than a correct one for free.

### 3. Decide where the flag lives, knowing perms are shell-deep

FAT permissions are advisory. They live in spare dirent bytes and are enforced
only by the shell's `may()`. Anything that reaches the VFS or the raw device
sidesteps them by construction. So a flag file is only ever as protected as
the weakest path to it.

If the intended solve is "get to ring 0", the flag should not be a file at
all. Hold it in kernel memory and have the kernel disclose it only when
`cur_uid == 0`. Then stage 3 is genuinely required rather than nominally.

---

## What is actually exploitable

Verified this session, in the current tree.

| Surface | State | Use |
|---|---|---|
| `int 0x80`, 9 calls | `user_ok()` bounds-checks every user pointer | The place to plant the flagship bug |
| Single address space | Kernel identity-mapped low, user at `0xC0000000` | Any arbitrary write is total; no ASLR, no NX, no stack cookies |
| `cur_uid` | file-scope static in `users.c` | The cleanest win condition: flip it to 0 |
| soupyc | Tree-walking interpreter running in **ring 0** | A memory bug here is instant kernel compromise |
| `exec_elf` | Maps `PT_LOAD` from a player-supplied file | The loader can be made to write for you |
| AlphaSOUP-32 | 32-bit unsalted password hash in `/etc/kitchen` | Brute-forceable in seconds |
| Preemption | On since v0.7.4; only `heap.c` was made safe | Real races exist in `fat.c`, `vga.c`, `ata.c` |
| `spill` | Divide-by-zero panic dumps registers, CR2, stack walk | Free address disclosure |

The absence of mitigations is the point. The difficulty should come from
understanding an unfamiliar kernel, not from defeating ASLR that isn't there.

---

## Challenge A: Mise en Place

**Warm-up.** Build effort: none, it already works.

*You are a line cook. The recipe is in the head chef's office.*

**The bug:** the permission bypass above. `pour /FLAG.TXT` is refused by
`may()`; soupyc's `open()` was never taught to ask.

**Intended solve:**
1. `menu`, notice `soup <file>` runs a script
2. `jot X.SC`, write three lines using `open`/`read`
3. `soup X.SC`

**Why it works as an opener:** it rewards reading the built-in help and
noticing that two commands that both "open files" disagree about who may. It
puts everyone on the board without giving away anything structural.

---

## Challenge B: Salt to Taste

**Medium.** Reversing + brute force. Build effort: low, the code exists.

*The staff list is public. The hashes are only 32 bits.*

**The bug:** `/etc/kitchen` stores `name:uid:hexhash`. `alphasoup_hash()` is a
32-bit function and there is no salt. The `chef <command>` elevation path
checks a hash, not a password.

**Intended solve:**
1. Read `/etc/kitchen` (via A, or leave it world-readable)
2. Recover `alphasoup.c` from the handout or the binary
3. Brute-force **any** preimage for headchef's hash, seconds on a laptop
4. `chef <command>` with it

**The insight worth testing:** you do not need the real password. Any string
that collides is accepted, because the check is `hash(input) == stored`. That
is the lesson, and it is a good one.

---

## Challenge C: Second Helping

**Hard. Deterministic.** The flagship pwn candidate. Build effort: medium.

*Ring 3 is a real boundary here. Cross it.*

**The bug to plant:** make `user_ok(ptr, len)` compute `ptr + len` and compare
against `USTACK_TOP` without checking for wrap. A large enough `len` overflows
the sum, and the check passes for a pointer that is nowhere near user memory.

**Intended solve:**
1. `SYS_WRITE(fd=2, buf=<kernel address>, len=huge)` streams kernel memory to
   the serial log: an arbitrary read
2. Locate `cur_uid` in the dump
3. `SYS_READ(fd=0, buf=&cur_uid, ...)` to write a zero over it: an arbitrary
   write
4. `whoami` reports headchef; take the flag

**Why it is solvable:** nine syscalls, a memory map already documented in
`CLAUDE.md`, and no mitigations. The chain is long but every link is plain.
Fires every time, so nobody loses to luck.

**Cost:** players must write and deliver a ring-3 ELF. See the delivery note
under Challenge D.

---

## Challenge D: Bad Recipe

**Hard. Elegant.** Build effort: medium. Read the delivery note before choosing this.

*`cook` runs ELF binaries. The loader trusts the file.*

**The bug to plant (or confirm):** `exec_elf` maps each `PT_LOAD` at its
`p_vaddr` without checking that it lands above `USER_BASE`. An ELF whose
segment header points at a kernel address makes **the loader itself** write
attacker-controlled bytes into ring 0, before a single instruction of the
program runs.

**Intended solve:** hand-craft an ELF32 whose `PT_LOAD` targets `cur_uid` (or a
syscall handler pointer) with the bytes you want written. The exploit is the
file. There is no runtime bug to trigger.

**Delivery note, and it applies to C as well:** soupOS has no upload. A player
must materialise a binary on a FAT16 volume using only `jot` (text) and soupyc
(`open`/`write`/`chr`, capped at 47-character strings). Writing an ELF header
byte by byte from a script is a real puzzle in its own right.

Decide deliberately whether that is *part* of the challenge or just friction.
If friction, stage the binary on the disk image and let them `decant` it. If
you keep it, it is genuinely novel and nobody will have seen it before.

---

## Challenge E: Too Many Cooks

**Hard. Most thematic. Lowest build cost.** My pick for the flagship.

*The scripting language runs in ring 0.*

**The bug to plant:** soupyc's arrays are a static pool of 24 by 48, indexed by
`int`. Remove the negative-index check on assignment in `N_INDEX`, and
`a[-N] = x` writes *below* the pool: into interpreter state first, then the
kernel heap.

**Intended solve:**
1. Work out the pool's base address (`taste`, if you leave it; otherwise a leak)
2. Compute the offset from the pool to the target
3. `a[-offset] = value` to overwrite `cur_uid`, a saved return address, or a
   function pointer
4. Ring 0

**Why this is the best flagship:**

- **Delivery is free.** The exploit is a text file. `jot` writes it. No ELF
  crafting, no upload problem, none of Challenge D's friction.
- **The source is small.** `soupyc.c` is one file a player can actually read.
- **It is the most soupOS-shaped bug there is.** The OS's own scripting
  language, the thing the whole project is proudest of, is the way in. That
  reads as designed rather than bolted on.
- **One removed bounds check** is the entire diff.

---

## Challenge F: Service Rush

**Very hard, and flaky. Bonus at most.** Build effort: none to create, high to make fair.

*v0.7.4 armed timer preemption. Only the heap was made safe.*

**The bug:** already real. `fat.c` carries 44 file-scope statics, `vga.c` and
`ata.c` more, and IRQ0 can now interrupt any of them mid-operation. A
background task racing a foreground FAT write can corrupt directory entries,
including the owner and mode bytes.

**Intended solve:** run `bgclock` or `yieldbg`, race it against `stir`, and
corrupt a dirent so you own the flag file.

**Do not make this the flagship.** Races that fail four times in five read as a
broken challenge, not a hard one, and you will spend the event fielding
tickets. Worth keeping as an unscored bonus, or as the last stage of a chain
where players already have the flag and are going for style points.

---

## If you want one chained challenge

1. **Mise en Place**, read `/etc/kitchen`. Everyone gets on the board.
2. **Salt to Taste**, crack the 32-bit hash. Become headchef.
3. **Too Many Cooks**, reach ring 0 and read a flag that was never a file.

Keep the real flag in kernel memory, disclosed only when `cur_uid == 0`. That
makes stage 3 load-bearing instead of decorative, and it sidesteps the
advisory-permissions problem entirely.

Difficulty lands about right: stage 1 is a warm-up, stage 2 is a solid
mid-tier crypto/RE problem, stage 3 is a genuine kernel exploitation task
against an unfamiliar target with the source in hand.

---

> **Decided 2026-08-26:** ship the four-stage chain Mise en Place -> Salt to
> Taste -> Bad Recipe -> Too Many Cooks. Build plan, including how Bad Recipe
> and Too Many Cooks were split so they do not both end at the same primitive,
> is in [`challenge-chain-plan.md`](challenge-chain-plan.md).

## Decisions still needed

**Handout.** Recommendation: full source plus the vulnerable diff. Kernel pwn
challenges normally ship the patch. soupOS is 30-odd unfamiliar files, and
making players reverse the whole thing *and* build the exploit is what tips
this from hard into unsolvable in a weekend. The difficulty should live in the
exploit, not in orientation.

**One challenge or a chain.** A chain scores better across a wide field: weak
teams clear stage 1, strong teams finish. A single hard challenge risks a
zero-solve, which is a bad outcome for a challenge you want people to remember.

**Is the soupyc permission bypass a bug or Challenge A?** It cannot be both.
Decide before anything else, because it changes what "close these first" means.

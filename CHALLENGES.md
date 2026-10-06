# The four stages

These are the prompts exactly as players saw them at CDCTF 2026. One booted
soupOS instance serves the whole chain — you do not restart between stages.

Log in at the boot prompt:

```
cook:   cook
secret: soup
```

The handout is the source (this repo), lightly redacted, plus a symbol map of
the kernel you build (`nm kernel.elf`). Every stage is findable from source.

**Two things that will otherwise cost you an hour each:**

- **The root bowl is not writable by a cook.** Work in `/prep`, which exists
  for exactly this reason. `cd prep`.
- **Programs run with `cook` resolve paths from the root, not your current
  bowl.** `cook /unhex.elf T.HEX ...` fails; `cook /unhex.elf /prep/T.HEX
  /prep/T.ELF` works. This trips everyone once.

Four flags, in order. Each one gives you something the next one needs.

---

## Stage 1 — Mise en Place  *(pwn, 100)*

There is a flag file in the root bowl and you cannot read it. `serve /` will
tell you why.

Two commands in this system open files. Only one of them asks permission.

---

## Stage 2 — Salt to Taste  *(pwn, 200)*

Only the headchef knows today's special.

The kitchen roster is world-readable and stores password hashes. The hash is
32 bits wide, has no salt, and the login check compares hashes rather than
passwords. You do not need the headchef's password. You need something that
hashes like it.

The hash function is in the source.

---

## Stage 3 — Bad Recipe  *(pwn, 400)*

soupOS runs ELF binaries and validates where a segment writes. It is less
careful about where a segment reads from.

Somewhere in kernel memory there is a string that is never written to disk.
Build a binary that convinces the loader to hand it to you.

There is no upload. `/unhex.elf` reads hex text and writes the raw bytes, and
the `jot` editor will take as much text as you want to type. A soupyc script
cannot do this job: its strings are NUL-terminated, and an ELF header is full
of NULs.

---

## Stage 4 — Too Many Cooks  *(pwn, 500)*

soupyc runs in ring 0.

Its array assignment checks that your index is not too large. It does not
check that your index is not too small.

There is a routine in this kernel that nothing ever calls. Find its address
(`nm kernel.elf`).

---

Stuck? Full writeups are in [`solutions/SOLUTIONS.md`](solutions/SOLUTIONS.md)
— but they spoil everything, so try first.

# soupOS — a four-stage pwn challenge

soupOS is a 32-bit operating system written from scratch in C and NASM: its
own bootloader, FAT-ish filesystem, permission model, ELF loader, a tiny
scripting language called **soupyc**, a VGA mode-13h stack, and a working port
of **Doom**. It has a kitchen theme and no mitigations whatsoever — no ASLR,
no stack canaries, no NX, no SMEP. That is not the difficulty. The difficulty
is finding the four places where the kitchen trusts the wrong thing.

This repo is the version that ran as a chained CTF challenge at **CDCTF 2026**,
packaged so you can boot it and try the four stages yourself. The whole machine
runs in a terminal (serial console) or a browser tab (VNC).

```
  ┌─ Stage 1  Mise en Place   two commands open files, one checks permission
  ├─ Stage 2  Salt to Taste   a 32-bit unsalted hash, compared as a hash
  ├─ Stage 3  Bad Recipe      the ELF loader checks where a segment writes,
  │                           not where it reads from
  └─ Stage 4  Too Many Cooks  a script array bounds-checks the top, not the
                              bottom — and the interpreter runs in ring 0
```

Each stage is findable **from the source in this repo**. The four prompts are
in [`CHALLENGES.md`](CHALLENGES.md). Full writeups (spoilers) are in
[`solutions/`](solutions/).

---

## Quick start (QEMU, in your terminal)

You need a Linux toolchain with a 32-bit-capable gcc. On Debian/Ubuntu:

```bash
sudo apt install build-essential nasm qemu-system-x86 grub-pc-bin xorriso mtools
```

Build the **challenge** image and boot it on the serial console:

```bash
make CHALLENGE=1            # build the kernel + ISO with the bugs re-opened
make CHALLENGE=1 disk       # build the data disk
make CHALLENGE=1 run        # boot in QEMU, shell on your terminal
```

> **Read this or nothing is solvable.** `CHALLENGE=1` is **not** the default.
> A plain `make` builds *normal* soupOS with every bug patched and the
> challenge code left out — it boots, but none of the four stages exist. Always
> pass `CHALLENGE=1` to all three steps, and run `make distclean` first if you
> previously built without it (the Makefile refuses to mix objects, but a stale
> tree can still bite).

Log in at the boot prompt:

```
cook:   cook
secret: soup
```

Quit QEMU with `Ctrl-A X`.

### Doom (optional)

The build expects `DOOM1.WAD` (the Doom shareware data file) in the repo root.
It is **not** included — it is not ours to ship. Drop your own copy in and it
gets baked onto the disk; without it the build just skips Doom and boots fine.
Doom is irrelevant to the challenge; it is here because the OS can run it.

```bash
cp /path/to/DOOM1.WAD .
make CHALLENGE=1 disk       # now the disk includes the WAD
```

---

## Play it in a browser (Docker)

The same image the event served players: QEMU behind noVNC, one throwaway VM
per instance.

```bash
make CHALLENGE=1 && make CHALLENGE=1 disk
docker build -f challenge/Dockerfile.web -t soupos-web .
docker run --rm -p 8080:8080 soupos-web
# open http://localhost:8080
```

There is also a plain `nc`-style TCP build (`challenge/Dockerfile`) that serves
the serial console over a socket — see [`challenge/README.md`](challenge/README.md)
for how the event wired both, including the QEMU hardening (`-monitor none`,
`snapshot=on` per connection, concurrency caps).

---

## About the flags

The four stage flags are redacted to `cdctf{REDACTED_STAGE_N...}` in the
source ([`src/challenge.c`](src/challenge.c)). When you solve a stage on your
own build, that redacted string **is** your proof the exploit worked — the
mechanic is identical to the event. The real flag strings are revealed in
[`solutions/SOLUTIONS.md`](solutions/SOLUTIONS.md).

---

## Where to look

| Path | What |
|---|---|
| [`CHALLENGES.md`](CHALLENGES.md) | the four stage prompts, login, and the two gotchas that cost everyone an hour |
| [`src/`](src/) | the whole kernel. The planted bugs live in `challenge.c`, `soupyc.c`, `usermode.c` (the ELF loader), `vfs.c`, and `alphasoup.c` |
| [`CLAUDE.md`](CLAUDE.md) | full architecture notes — memory map, syscalls, the filesystem, soupyc |
| [`ROADMAP.txt`](ROADMAP.txt) | the changelog, back to the first boot |
| [`challenge/`](challenge/) | Docker packaging for the TCP and web deployments |
| [`solutions/`](solutions/) | **spoilers** — full writeups and the stage-3 exploit builder |

## A few things worth knowing before you dive in

- **The root bowl is not writable by a cook.** Work in `/prep`. `cd prep`.
- **Programs run with `cook` resolve paths from the root, not your current
  bowl.** `cook /unhex.elf T.HEX ...` fails; `cook /unhex.elf /prep/T.HEX
  /prep/T.ELF` works.
- The chain is ordered. Each stage hands you something the next one needs.

## License

MIT — see [`LICENSE`](LICENSE). Doom is not included and is not covered by it.

Have fun in the kitchen.

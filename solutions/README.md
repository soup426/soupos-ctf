# Solutions — SPOILERS

Everything in this folder gives away the challenge. If you want to solve
soupOS yourself, close this folder and go read [`../CHALLENGES.md`](../CHALLENGES.md).

Still here?

- [`SOLUTIONS.md`](SOLUTIONS.md) — full writeup of all four stages, with the
  real flag strings, the bugs quoted from source, and notes on how the
  build-specific numbers (hashes, heap offsets, kernel addresses) are
  re-derived if you rebuild the kernel.
- [`mkleak.py`](mkleak.py) — the stage 3 exploit: builds an ELF whose second
  `PT_LOAD` segment carries a hostile `p_offset`, making the loader copy
  kernel memory into the program's own address space. Run
  `python3 mkleak.py <offset> [size] > leak.hex`.

The stage 2 brute-forcer (`crack.c`) is **not** a spoiler and lives in the main
tree at [`../src/solve/crack.c`](../src/solve/crack.c), the way players got it.

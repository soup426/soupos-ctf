# Mincecraft: a voxel sandbox for soupOS (design)

2026-10-11. Asked for by the user: "Minecraft but called Mincecraft", run on
soupOS-processes. Chosen scope: **sandbox plus light survival**, world
**fully food-themed**. Personal OS work, not CTF work.

## What the user said, and what is assumed

Said: a Minecraft-like game named Mincecraft; first version is a sandbox with
light survival (an inventory filled by mining, gravity, a day and night
cycle); every block is food.

Assumed (open to correction):
- It runs as a ring-3 program in a desktop window (`cook mincecraft.elf`
  from a terminal on countertop), like `frost`, `swirl` and `scale`.
- First person, keyboard first. The desktop has no pointer capture, so the
  mouse cannot steer the view the way Minecraft's does.
- No mobs, health, hunger or crafting in this version (listed under
  "Later" below).

## Success criteria

1. `cook mincecraft.elf` opens a 640x480 window showing a generated food
   world in first person, at **15 frames a second or more under KVM**
   (measured, with a serial marker).
2. The player walks, jumps, falls and collides with blocks; breaking a block
   puts it in the inventory, placing one takes it out.
3. Day turns to night and back; night is visibly darker.
4. Quitting saves the world; the next `cook mincecraft.elf` comes back to the
   same world, player position, inventory and time of day.
5. `scripts/check.sh` passes, with Mincecraft's test scripts in its list.

## Constraints from the platform (measured 2026-10-11)

- **No FPU.** `UCFLAGS` has `-mgeneral-regs-only` (an SSE instruction
  triple-faults the machine, ROADMAP v0.7.4). All maths is 16.16 fixed point
  with 64-bit products, as `swirl.c` does.
- **Windows:** `SYS_WIN_OPEN` up to 640x480, `SYS_WIN_PUT` copies the whole
  body as 0x00RRGGBB, `SYS_WIN_EVENT` gives key presses, button
  down/move/up (move only while a button is held), wheel and close.
- **No key-up events.** Windows get key presses only, so held keys show up
  as typematic repeat (about 250 ms delay, then about 30 a second). Walking
  on that would stutter. The kernel already keeps `key_state[256]` (set on
  make, cleared on break, extended keys at index `0x80|code`) in
  `src/keyboard.c`; it is just not exposed to programs.
- **Memory:** image at 0xC0000000 (`MAX_ELF` 256 KB), heap from
  0xC4000000, 1 MB stack. The address range is large, but a process may have
  only **4 MB of pages resident** (`PROC_PAGE_CAP` 1024, `src/proc.h:33`);
  past that its pages swap. The game's budget is in "Memory budget" below.
- **Mouse:** only the left button reaches a program. `WEV_DOWN`/`MOVE`/`UP`
  are pushed for left presses and drags in the body (`countertop.c:2210`);
  a right press in a body is not passed on. So the mouse cannot steer the
  view without a desktop change, and this version does not make one.
- **No HOME.** Programs see only the variables the shell hands them
  (`build_env`, `shell.c:6001`). There is no HOME and no who-am-I call, and
  a relative open is taken from `/` (`usermode.c:489`).
- **Build:** `user/%.elf` compiles exactly `user/ulib.c user/$*.c`. A game
  in several files needs its own rule. The disk rule copies each program
  under an 8.3 uppercase name (`Makefile:248-305`), so the game's `mcopy`
  names `::mincecraft.elf` itself, the first program on the disk with a long
  name.
- **Names:** every program has a kitchen name. `mince.elf` (the hex
  dumper) and `scripts/mince-test.sh` exist already, so the game is
  `mincecraft.elf` (a long name; FAT long names are read and written since
  well before this) and its tests are `scripts/mincecraft-*-test.sh`.
- **Font:** programs may include `src/font8x16.h`, as `scale.c` does.

## Approaches considered

1. **Per-pixel 3D DDA ray casting (chosen).** One ray per pixel walks the
   block grid cell by cell (Amanatides and Woo) until it hits a solid block or
   runs out of distance. Occlusion, caves, overhangs and placed blocks all
   come for free, the code is small (a few hundred lines), and it is easy to
   test. Cost is pixels times steps, bounded by a view distance with fog.
2. **Face rasteriser with a z-buffer.** Mesh each chunk's visible faces,
   project and fill triangles. Faster for long views, but needs clipping,
   perspective-correct texturing and remeshing on every edit: three or four
   times the code for a view this small.
3. **Heightmap "voxel space" (Comanche style).** Very fast, but one height
   per column: no caves, no overhangs, no placing a block in the air. Not
   Minecraft.

Approach 1 at 320x240, scaled 2x into the 640x480 window, with a 24-block
view distance. Measure first (queue rule 3): a throwaway render spike (a
generated world, a fixed camera, the fps marker) comes before any of the
rest is built. If 15 fps is not reached, the fallbacks in order are a
20-block view, then 256x192 internal resolution. The reviewer's estimate:
about 77k rays, 20 to 40 steps each, three divisions per ray, roughly 60 to
120 M cycles a frame, so it should be reachable under KVM.

## Design

### Units

The game lives in `user/mincecraft/`, built by its own Makefile rule into
`user/mincecraft.elf` and copied to the disk with the other programs.

| File | Does | Depends on |
|---|---|---|
| `fix.h` / `fix.c` | 16.16 type, mul/div through 64 bits, a 1024-entry sine table (cosine by offset), reciprocal | nothing |
| `world.h` / `world.c` | the block grid, `world_get/set`, generation from a seed | `fix` |
| `blocks.h` / `blocks.c` | the block table: name, colours, solid, hardness, texture pattern | nothing |
| `render.c` | ray caster into a 320x240 buffer, 2x scale-up, fog, day/night tint | `world`, `blocks`, `fix` |
| `hud.c` | crosshair, hotbar with counts, the block name under the crosshair, clock | `blocks`, `font8x16.h` |
| `player.c` | position, view angles, AABB physics, the targeted block (a ray from the eye, 5 blocks) | `world`, `fix` |
| `save.c` | the world file: write and read | `world`, `player` |
| `main.c` | window, event and key loop, fixed-tick timing, markers | everything |

Each can be read without the others' insides. The test hooks reach
`world`, `player` and `save` through `main.c`'s command-line modes, never
around them.

### The world

- **Size:** 128 x 128 columns, 64 high, one byte per block (1 MB on the
  heap). The edges are walls of cast iron that cannot be broken, so nobody
  walks off the edge.
- **Blocks** (id: name, what it plays as):

  | id | Name | Plays as | Look |
  |---|---|---|---|
  | 0 | air | | |
  | 1 | crust | grass top | golden brown, darker speckles |
  | 2 | crumb | dirt | pale bread interior, holes |
  | 3 | parmesan | stone | off-white, grainy |
  | 4 | cast iron | bedrock, unbreakable | near-black |
  | 5 | broth | water, not solid; walking through it is slow | translucent amber |
  | 6 | sugar | sand, by the broth | white, sparkly |
  | 7 | broccoli stalk | wood | pale green, rings on top |
  | 8 | floret | leaves | dark green, bumpy |
  | 9 | peppercorn | ore in the parmesan | parmesan with black dots |
  | 10 | butter | rare, deep | yellow, smooth |

  Textures are 8x8 patterns per face made from a hash of (block, face,
  texel), so there is no image data to ship.
- **Generation** from a 32-bit seed (from `SYS_RANDOM` for a new world, or
  `-seed N`): two octaves of value noise on a lattice, integer only, give a
  height of 20 to 44; crust on top, three of crumb, parmesan below, cast
  iron at y 0. Columns below height 26 fill with broth up to 26, with sugar
  where broth meets land. Broccoli (stalk 4 to 6 high, a floret ball on top)
  on about 1 in 60 crust columns. Peppercorn veins in parmesan, butter below
  y 12. Each step is integer maths on the seed, so a Python copy can
  reproduce it bit for bit.

### Rendering

- Camera: position (16.16), yaw and pitch as table indices; pitch clamped
  to plus or minus 80 degrees. A 70-degree field of view.
- Per pixel: direction from yaw, pitch and the pixel's offset, then DDA
  through the grid up to 24 blocks. A direction component near zero gives
  a clamped step (`tDelta` held at a large constant, since 16.16 overflows
  past 32767), and a ray at or above y 64 going up stops at once as sky.
  Fog depth is the distance along the camera axis. A hit gives the block, the face and the
  hit point's fraction, which picks the texel. Faces shade by axis (top
  100%, sides 80% and 65%, bottom 50%). Broth is drawn as a 50% blend over
  what lies behind it (the ray carries on through broth once).
- Fog: the last 8 blocks fade into the sky colour.
- Day and night: a 20-minute day. The sky colour and a light factor (100% at
  noon, 30% at midnight) come from the time; every pixel is multiplied by
  the light factor. No per-block light (no torches in this version).
- The 320x240 buffer is doubled into the 640x480 body and handed over with
  one `SYS_WIN_PUT` a frame. The HUD is drawn after the scale-up, at full
  resolution.

### Input: one new syscall

`SYS_WIN_KEYS` (24): `ebx` = window id, `ecx` = a 32-byte buffer. It fills
the buffer with a bitmap of the 256 `key_state` entries **only if that
window belongs to the caller, is placed, is the topmost (focused) window,
and the desktop is not locked**; otherwise it writes all zeros. It returns
0, or -1 for a bad id, a window that is not the caller's, or a buffer the
caller does not own.

The kernel side is `countertop_win_keys(pid, id, buf)` in `countertop.c`:
it takes `app_mtx`, checks ownership with `app_mine` (as `SYS_WIN_EVENT`
does, `countertop.c:516`), and requires `a->win >= 0 && a->win ==
topmost() && !locked`. `a->win` is -1 until the desktop has placed the
window (`countertop.c:1786-1798`), so a new window reads as unfocused until
then. **The lock check is load-bearing:** keys typed at the cloche lock
screen go to the lock, but `key_state` still records them, so without it a
program could read the unlock password as it is typed. `ulib` gets a
`sys_win_keys` wrapper and its own scancode constants (W 0x11, A 0x1E,
S 0x1F, D 0x20, space 0x39, arrows 0x80|0x48 and so on), without including
the kernel's `keyboard.h`.

Controls:

| Key | Does |
|---|---|
| W A S D | walk (held, through `SYS_WIN_KEYS`) |
| arrow keys | look (held) |
| space | jump (held: jumps again on landing) |
| F, or the left button held in the body | break the targeted block (progress by hardness, from `WEV_DOWN` until `WEV_UP`) |
| R or Q | place the hotbar's block on the targeted face |
| 1 to 9, wheel | choose a hotbar slot |
| P | save now |
| Esc or the close box | save and quit |

Discrete actions (place, slot, save, quit) come from `WEV_KEY` presses, so a
held R does not lay a row of blocks.

### The player and light survival

- An axis-aligned box 0.6 wide, 1.8 high, eye at 1.6. A fixed 20 Hz tick
  (from `SYS_TICKS`, 100 Hz: one tick per 5), rendering between ticks.
  Gravity, a terminal speed, jump only when standing, collision resolved one
  axis at a time. Broth halves speed and gravity and lets space swim up.
- **Breaking** takes `hardness` ticks held on the same block (crumb 6,
  parmesan 20, peppercorn 24, cast iron never). The block goes into the
  inventory; there is no item entity on the ground.
- **Inventory:** a count per block id (up to 999). The hotbar shows the
  first nine kinds you own, in id order, with their counts. A new world
  starts empty, so you must mine before you build.
- **Placing** needs a count above zero and refuses a cell that overlaps the
  player's box.
- Spawn: the centre column, on top of its highest solid block.

### Saving

- Path: `$HOME/mincecraft.wld`, or the file given by `-world PATH`.
  **This needs a shell change:** `build_env` always hands `HOME=<the
  cook's home>` (the path `ensure_home` makes, `shell.c:352`; the
  headchef's is `/`), ahead of the variables the cook handed themselves. A
  hand-set HOME wins. `scripts/env-test.sh` lists the whole environment in
  its E1 to E5 checks, so those listings change with it, and it gains a
  check that HOME is there for a cook who never set it. Without HOME and
  without `-world`, the game says so and exits 1.
- The save is written through a 4 KB buffer, never built whole in memory
  (the worst-case run-length encoding is 2 MB).
- Format (little-endian): magic `MINC`, version 1, seed, time of day,
  player position and angles, 11 inventory counts, then the 1 MB grid
  run-length encoded as (count, id) byte pairs, then a 32-bit sum of every
  byte before it. A file whose magic, version, length or sum is wrong is not
  loaded: the game says so on the desktop bar (`SYS_NOTE`) and starts a new
  world in memory. The bad file is left alone until the player saves (P or
  Esc), which overwrites it. A typical world compresses well under 100 KB.
- Saved on P, on Esc, and on the close box; never on a kill (no handler for
  it), which the docs say.

### Memory budget

Resident pages must stay under the 4 MB cap, or the game swaps every frame:

| Item | Size |
|---|---|
| block grid | 1.0 MB |
| 640x480 body handed to `SYS_WIN_PUT` (the whole body each call, `usermode.c:703`) | 1.2 MB |
| 320x240 render buffer | 0.3 MB |
| image, stack in use, save buffer | under 0.3 MB |
| **total** | **about 2.8 MB** |

On the kernel side, the desktop kmallocs a copy of the body from the 8 MB
kernel heap (`heap.c:22`, `countertop.c:529`). Mincecraft is the first
program to open a 640x480 window, so the spike logs the heap's free space at
open (the desktop already logs it, `countertop.c:541`) to see that 1.2 MB
fits beside the desktop's other windows.

### Errors and limits

- `SYS_WIN_OPEN` fails: with no desktop running, the program prints
  "Mincecraft needs the countertop (start it first)"; with one running, it
  prints "the countertop refused the window". Either way it exits 1.
- Heap: the grid and both frame buffers are taken with `sbrk` at start; a
  failure prints and exits 1.
- A full disk or failed write: a note on the bar, and the game keeps
  running.

### Serial markers and test modes

Markers go to fd 2 (serial) as `[mincecraft] ...`, as other programs do:
`window up`, `world seed=N hash=H`, `fps=N` (every 5 s), `player x= y= z=`
(on request), `inv` lines, `saved bytes=N sum=S`, `loaded ...`, `bye`.

Command-line modes for the tests:
- `-seed N`: a fixed world.
- `-time T`: start at time of day T (0 is midnight, 600 noon, in a 1200
  step day), for the day and night screendumps.
- `-gen`: generate, print the hash marker, exit (no window needed).
- `-script`: read moves from `SYS_WIN_EVENT` as usual but print the player
  marker after every tick, so key-driven tests can check physics exactly.

### Testing

Each new script goes in `check.sh`'s list.

1. `scripts/winkeys-test.sh`: `SYS_WIN_KEYS` from a small helper mode in
   `prod.elf`: held keys appear while focused, zeros once another window has
   the focus, **zeros while the desktop is locked** (a key held at the
   cloche), -1 for a bad id, another program's id and a bad buffer.
   Held keys are driven with `qemu_keys.py`'s `KEY:w 1000` (a raw
   `sendkey` with a hold time) followed by `WAIT:2`, because the command
   returns at once. QEMU's PS/2 keyboard does not auto-repeat, so typematic
   repeat is not exercised by the tests.
2. `scripts/mincecraft-gen-test.sh`: `-gen -seed` for three seeds against a
   Python copy of the generator (the world's FNV-1a hash, and selected
   columns), as `swirl-test.sh` checks swirl.
3. `scripts/mincecraft-view-test.sh`: a fixed seed and spawn, screendumps:
   at noon the top rows are the sky colour, the crosshair pixels are there,
   the centre pixel is a crust or parmesan colour family; at midnight the
   same pixel is darker by the light factor (`-time 600` and `-time 0`).
   In `check.sh`, which runs tests side by side, the fps marker need only be
   5 or more. The 15 fps criterion is measured on an idle machine, recorded
   in the changelog, and cross-checked against the desktop's puts/drawn
   report (`countertop.c:497`).
4. `scripts/mincecraft-play-test.sh`: hold W for a second (QEMU `sendkey`
   with hold time), the player marker has moved forward and stayed on the
   ground; jump goes up and comes back; break the block underfoot and the
   inventory marker counts it; place it back and the count is 0 again.
5. `scripts/mincecraft-save-test.sh`: move, break, P, Esc; start again;
   the loaded marker's position, inventory and world hash match the saved
   ones. A corrupted copy (one byte flipped) is refused with the note.

## Later (not in this version)

Crafting (e.g. stalk to planks of "toast"), health and hunger (the kitchen
practically asks for it), mobs, torches and per-block light, worlds larger
than 128x128, a menu of saved worlds, sound (`SYS_TONE` for breaking).

## Risks

- **Speed** is the main one; the fps marker and the fallbacks above deal
  with it. TCG (no KVM) will be slow; the tests run under KVM, as the rest do.
- **Typematic repeat** still sends `WEV_KEY` presses while a key is held;
  the game ignores presses of the movement keys and reads them from the
  bitmap only.
- **Kernel heap:** a 640x480 body copy is the largest the desktop has held;
  measured in the spike (see "Memory budget").
- **Build rule:** the explicit `user/mincecraft.elf` rule carries the
  pattern rule's prerequisites (`ulib.c`, `ulib.h`, `user.ld`,
  `syscall_nr.h`, `rx.c`; `Makefile:119`) plus its own sources, and the
  ELF is added to `USER_ELFS`.

# soupOS — Developer Context for AI Agents

## What this is

soupOS is a 32-bit x86 hobby OS written in C and NASM assembly (currently
**v0.7.6**). It boots via GRUB and runs in protected mode with paging, a
physical/heap allocator, and a cooperative scheduler. On top of that:

- **Filesystem**: FAT16 read/write with subdirectories ("bowls") and a VFS shim
  over files + `/dev` nodes. File commands incl. `decant` (cp) / `relabel` (mv).
- **Multi-user**: accounts ("kitchen staff") in `/etc/kitchen`, a login gate,
  and advisory rwx/owner permissions stamped into FAT dirents.
- **Shell + tools**: line-editing shell, `jot` full-screen text editor, and a
  `soupyc` scripting language (functions, arrays, file I/O, `include`, builtins,
  triple-quoted multi-line strings, line-numbered errors; strings cap at 47 ch).
- **`ai <prompt>`**: a COM2 serial bridge to a host-side LLM (`make run-ai`).
- **Ring-3 user mode (v0.7.0)**: `cook <prog.elf>` loads and runs ELF32 programs
  in ring 3 over an `int 0x80` syscall interface, with real (U/S-bit) memory
  protection. See the "User mode" section below.

Theming note: commands are kitchen-flavoured (`pour`/`stir`/`serve`/`cook`/…).

> **Doom port: re-enabled 2026-08-26 (v0.7.5).** `doom.c/doom.h/wad.c/wad.h`
> live in `src/` again and the `doom` shell command is back. It **refuses to
> run while any other task is alive**, because doom.c has no `task_yield()` in
> its gameplay path and only `heap.c` is preemption-safe. See
> `docs/doom-reenable-plan.md` for why refusing beats `preempt_disable()` here.
> Needs `DOOM1.WAD` on the disk (`make wad`). The enemy AI regression noted in
> `ROADMAP.txt` is still open.

The OS has **no libc**. All standard functions (memcpy, strlen, etc.) are in `src/doom_libc.c` / `src/str.c`. Everything is freestanding.

---

## Build & Run

```bash
# Build ISO
make

# Build ISO + run in QEMU (interactive VGA window)
make run

# Run with the AI bridge attached on COM2 (see "AI serial bridge")
make run-ai            # AI_FAKE=1 make run-ai  for offline canned replies

# Build a ring-3 user program (user/hello.c -> user/hello.elf)
make user

# Rebuild FAT disk image (wipes disk.img; includes the .SC demos + HELLO.ELF)
make distclean && make disk

# Add DOOM1.WAD to existing disk image
make wad
```

**Headless smoke test** (the oracle — use this as the gate after any change):
```bash
./scripts/smoke-test.sh          # boots, drives the shell, greps serial markers
KEEP=1 ./scripts/smoke-test.sh   # keep the serial log for inspection
```
It boots with `-display none`, types into the guest through the QEMU monitor
(`scripts/qemu_keys.py`), and checks boot, preemption arming, all four ring-3
syscall paths, scheduler interleaving, and that nothing panicked. Non-zero exit
on the first missing marker.

**Requirements:** `gcc` (32-bit multilib), `nasm`, `grub-mkrescue`, `xorriso`, `qemu-system-i386`, `mtools`
(on Arch/Manjaro: `pacman -S nasm libisoburn mtools` — `xorriso` ships inside `libisoburn`)

> **Do not remove `-mgeneral-regs-only` from `CFLAGS`/`UCFLAGS`.** GCC 12+
> auto-vectorises ordinary byte loops at `-O2`. The kernel never enables SSE
> and QEMU's default `qemu32` CPU has no SSE2, so a single emitted `pxor`
> triple-faults the machine at boot — before the IDT exists, so there is no
> panic message, just a hang. This bit us on GCC 16 (545 SSE instructions in
> the kernel, 83 per user ELF). See `ROADMAP.txt` v0.7.4.

**Note:** `make` builds the kernel + ISO but **not** the ring-3 programs, and
`make disk` is a no-op when `disk.img` already exists. After changing anything
under `user/`, run `make user` and re-`mcopy` the ELFs (or `rm disk.img &&
make disk`) — otherwise you are testing the stale copies on the old image.

**QEMU invocation** (from Makefile):
```
qemu-system-i386 -drive file=disk.img,format=raw,if=ide,index=0 \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown \
    -display gtk -serial file:/tmp/soupos-serial.log
```

**Screenshots** land in `~/Documents/Screenshot_*.png` when taken from QEMU's GTK window.

---

## Source Layout

```
src/
  boot.asm          GRUB multiboot entry point
  kernel.c          Main kernel init, calls shell
  gdt.c / gdt_flush.asm   GDT setup (kernel + user segments + TSS)
  idt.c / isr.c / isr_stubs.asm   IDT + IRQ handlers + panic + int 0x80
  usermode.c / usermode.h / usermode_asm.asm  ring-3 + syscalls + ELF loader
  syscall_nr.h      syscall numbers (shared with user programs)
  serial.c / serial.h   COM1 UART (polled, klog mirror) + COM2 (AI bridge)
  ai.c / ai.h       `ai` serial bridge to a host LLM over COM2
  klog.c / klog.h   Kernel log ring buffer (mirrors to serial)
  pmm.c             Physical memory manager (bitmap allocator)
  paging.c          x86 paging (identity-mapped for now)
  heap.c            kmalloc/kfree (linked-list heap)
  vga.c / vga.h     80×25 text-mode VGA
  vga13h.c / vga13h.h   VGA mode 13h (320×200, 256-color)
  keyboard.c / keyboard.h   PS/2 keyboard driver + scancode map
  timer.c / timer.h   PIT timer (100 Hz), timer_get_ticks()
  speaker.c         PC speaker beep
  ata.c             ATA PIO disk driver
  fat.c             FAT16 read/write + subdirectories ("bowls")
  vfs.c / vfs.h     VFS shim — uniform open/read/write over FAT + /dev
  users.c / users.h Kitchen staff — accounts, login, /etc/kitchen
  wad.c / wad.h     Doom WAD reader (index by lump name)
  doom.c / doom.h   Doom port (title, menu, level rendering)
  doom_libc.c       Freestanding C library (memcpy, etc.)
  shell.c           Interactive shell
  editor.c / editor.h  jot — full-screen text editor (writes 0xB8000 directly)
  soupyc.c          soupyc scripting language interpreter
  str.c             String utilities
  task.c / task.h   Scheduler (task_init, spawn, yield, exit, preemption)
  task_switch.asm   Context switch primitive (callee-saved reg + esp swap)
scripts/
  smoke-test.sh     Headless boot + shell-driver test (the gate)
  qemu_keys.py      Types into a running guest via the QEMU monitor
```

---

## Scheduler (preemptive, ring 0)

soupOS has a single-ring round-robin scheduler. A task gives up the CPU
either voluntarily — it (or a blocking call it makes) invokes `task_yield()`
— or involuntarily, when the 100 Hz timer IRQ preempts it (v0.7.4).

> **Preemption safety is incomplete.** Only `heap.c` guards its shared state
> (`irq_save`/`irq_restore` around the free list). `fat.c`, `soupyc.c`,
> `ata.c`, `vga.c`, `vfs.c`, `users.c` and `shell.c` all keep file-scope
> mutable state and are now interruptible at any instruction. Two tasks
> inside `fat_write`, or a background task printing while the shell redraws,
> are races. If you add a background task that touches a driver, wrap the
> critical region — and see the note on `preempt_disable` below before you
> reach for it. Tracked in `ROADMAP.txt` → KNOWN BUGS / DEBT.

### Model
- `task_init()` converts the booting kernel thread into task 0 ("kernel").
  Called once in `kernel_main` after `heap_init()`.
- `task_spawn(name, entry, arg)` allocates a 16 KB stack, seeds a synthetic
  frame so the first context switch "returns" into `entry(arg)`, links the
  task READY into a circular intrusive list, returns the `task_t *`.
- `task_yield()` reaps one dead task, picks the next READY peer, and calls
  `task_switch(&prev->esp, next->esp)`. No-op if no other task is ready.
- `task_exit()` marks current DEAD and yield-hlts forever. Reached
  automatically via the seeded return address when an entry function returns.

### Preemption (v0.7.4)
- `task_preempt_enable()` arms it. `kernel_main` calls it once, after the
  cooperative self-test and before `shell_run()`.
- `irq_handler` (`isr.c`) calls `sched_preempt()` on IRQ0 **after** sending
  the EOI (so the PIC is ready for the next tick) and with interrupts still
  disabled. `sched_preempt` switches only if preemption is armed, the
  scheduler is not already locked, and more than one task exists.
- `sched_locked` is the scheduler's own lock. `task_yield` takes it across
  the whole pick+switch, so a tick landing mid-switch sees it non-zero and
  skips. It balances *across* the switch: whoever is switched to runs the
  matching `sched_unlock` — after its own `task_switch`, or via the
  first-run trampoline `task_start` — so a running task always sees 0.
- ⚠️ `preempt_disable()`/`preempt_enable()` increment that **same global**
  counter. If a `preempt_disable` region ever yields, the decrement is run
  by whichever task is switched to, leaking the critical section into an
  unrelated task. Nothing calls it today, so this is latent — but making
  `sched_locked` per-task is a prerequisite for using it.
- Demo/proof: `yieldbg` (sleeper, `[bg]` markers every 300 ms) plus `spinbg`
  (busy loop, never yields, `[spin]` markers). The `[bg]` cadence is
  unbroken through the `[spin]` window only if IRQ0 is switching tasks.

### Context switch (src/task_switch.asm)
Saves only the callee-saved regs (EBX/ESI/EDI/EBP) — caller-saved regs are
already on the stack at the call site. Swaps ESP, pops regs, `ret`s.
15 instructions total. A task's entire suspended state is `task_t.esp`
pointing at its own stack's saved frame.

### Blocking-call contract
**Any loop that waits on external state must call `task_yield()`** — otherwise
other tasks starve. The canonical pattern is already in `keyboard_getchar`:
```c
while (!keyboard_available()) {
    task_yield();
    if (!keyboard_available())
        __asm__ volatile ("hlt");   /* wait for next IRQ */
}
```
`task_yield` before `task_init()` is a safe no-op, so new drivers can
unconditionally adopt this pattern.

All in-tree blocking loops now yield: `speaker_beep` uses `task_sleep`,
the ATA PIO wait loops `task_yield` every ~16k polls, and the shell's
`tick_delay` (wipe/bounce), `clock` and `cat` loops yield before
`hlt`. (Doom's VBlank wait is moot — Doom is parked.)

### Task lifecycle and reaping
- A task that returns from its entry falls through to `task_exit`
  (via its seeded stack frame) → state becomes DEAD.
- `task_yield` walks the list on each call and unlinks + `kfree`s one
  non-current DEAD task per pass. No leaks after a task exits normally.
- `task_kill(id)` just flips state to DEAD; the target is suspended, so
  the reaper picks it up on the next yield cycle. Refuses to kill
  `task_current()` or task 0 (the kernel).

### Key APIs
```c
void     task_init(void);                        // convert current → task 0
task_t  *task_spawn(const char *name,
                    void (*entry)(void *),
                    void *arg);
void     task_yield(void);
void     task_exit(void) __attribute__((noreturn));
task_t  *task_current(void);
task_t  *task_list_head(void);                   // walk via .next
task_t  *task_by_id(uint32_t id);
int      task_kill(uint32_t id);                 // 0 = ok, -1 = refused
char     task_state_letter(task_state_t);        // R/r/B/D for ps
uint32_t task_count(void);
```

### Shell surface
- `ps` — list tasks (id, state letter, stack size, name; current marked `*`).
- `kill <id>` — mark a task DEAD (reaped on next yield).
- `bgclock` — spawn a background task that writes HH:MM:SS to the top-right
  corner of the VGA text buffer. Proves tasks keep running while you type.

### OS-ification status
All four steps of `ROADMAP.txt` → "OS-IFICATION PATH" are complete
(scheduler, VFS shim, ring 3 + syscalls + ELF, panic + klog), and v0.7.4
added timer preemption on top. What remains is depth: making the drivers
preemption-safe, and per-process page directories so more than one user
program can exist at a time.

---

## Virtual Filesystem (VFS shim)

`src/vfs.c` is a thin layer that gives every consumer one uniform
open/read/write/seek/close API regardless of what backs a path. `fat_*`
is untouched — the VFS is **additive**, so existing shell commands did
not need rewriting. `vfs_init()` is called once at boot after `fat_init()`.

### Model
- `vfs_open(path, flags)` → `vfs_node_t *` from a fixed pool of
  `VFS_MAX_OPEN = 16` handles. A leading `/` is tolerated.
- `vfs_node_t.pos` / `.size` are authoritative for `vfs_tell/size/eof`;
  each backend keeps them in sync inside its `read`/`seek` ops.
- Backends supply a `vfs_ops_t` table (`read/write/seek/close`, any may
  be NULL). `vfs_read`/`vfs_write` enforce the open mode via the flag bits.

### Backends
- **FAT read** (`fat_read_ops`) — `vfs_open(name, VFS_RDONLY)` calls
  `fat_fopen`; reads/seeks delegate to `fat_fread`/`fat_fseek`.
- **FAT write** (`mem_file_ops`) — `vfs_open(name, VFS_WRONLY|VFS_CREATE)`.
  `fat.c` has no streaming write, so writes accumulate in a heap buffer
  that grows by doubling and is flushed with `fat_write()` on `vfs_close`.
- **Char devices** — `/dev/null`, `/dev/zero`, `/dev/serial` (writes to
  COM1), `/dev/kbd` (line-oriented blocking read via `keyboard_getchar`).
  `vfs_eof` always returns 0 for char devices.

### Surface
- `vfs_ls("/", ...)` merges the FAT root with the `/dev` nodes.
- Shell `vfstest` command exercises the whole API end to end.
- Adding a new device = one `vfs_ops_t` + one row in `devices[]`.
- soupyc's `open/read/write/close/eof` builtins are VFS consumers — a
  script can read/write disk files or stream to `/dev/serial`.

---

## Bowls, users & access control (v0.6.0)

soupOS is multi-user. Directories are called **bowls**; accounts are the
**kitchen staff**.

### Bowls (FAT16 subdirectories)
`fat.c` walks `/`-separated absolute paths. A directory is identified by
its first cluster, with `0` as a sentinel for the fixed FAT16 root; one
sector iterator (`dir_sector_lba`) serves both the root and cluster-chain
subdirs, and subdirs grow a cluster at a time when full. New bowls get
proper `.`/`..` entries. API: `fat_mkdir`, `fat_rmdir`, `fat_is_dir`,
`fat_exists`; `fat_ls` and every path call take an absolute path.
Shell: `cd`, `pwd`, `mkbowl`, `rmbowl`, `serve`, `bowltest`. The shell
tracks one `cwd` and resolves each argument against it; the prompt shows
it (`name@soupOS:/bowl>`). File management: `decant <src> <dst>` (copy)
and `relabel <src> <dst>` (move/rename). A destination naming an existing
bowl keeps the source's basename inside it (cp/mv-into-directory). Copy is
staged through one 64 KB buffer — `fat_write` has no streaming form, so
files larger than that are refused. `relabel` is copy + `fat_delete`.

### Users (`users.c`)
Accounts live in `/etc/kitchen`, one `name:uid:hexhash` line each (the
secret is an AlphaSOUP-32 hash). uid 0 is **headchef** (superuser);
everyone else is a **cook**. If `/etc/kitchen` is missing at boot it is
seeded with `headchef` / secret `soup`. `users_init()` runs from
`kernel_main`. The shell shows a login gate before the prompt
(`do_login`); `clockout` returns to it. Commands: `whoami`, `roster`,
`hire`, `fire` (headchef only).

### Access control
An owner uid and an rwx mode are stamped into two spare bytes of each FAT
directory entry — the `reserved` byte holds the mode (bit `FAT_PERM_MARK`
flags soupOS metadata; entries without it read back as headchef-owned,
default perms), `crt_time_tenth` holds the owner uid. API: `fat_stat`,
`fat_chmod`, `fat_chown`, `fat_set_creator`. Enforcement is **advisory**:
the shell's `may(path, 'r'|'w'|'x')` gates `pour`/`soup`/`stir`/`strain`/
`cd`/`serve`/`mkbowl`/`rmbowl`. The headchef bypasses all checks. Real
hardware enforcement needs ring 3 — see ROADMAP "OS-IFICATION" step 3.
`perms <file> [rwxrwx]` shows or sets a mode.

**Cosmetic note:** repurposing the `reserved` byte sets its VFAT
lowercase-name bits, so soupOS-created files display lowercase in other
FAT tools (mtools). soupOS is case-insensitive internally — harmless.

### Elevation
`chef <command>` runs one command as the headchef: it prompts for the
headchef secret, sets the session uid to 0 for the single `dispatch`
call, then restores it. Bare `chef` (no args) is still the CPU-info
command.

---

## Kernel log & panic

### Serial (`src/serial.c`)
Polled COM1 UART, 38400 8N1. `serial_init/putc/puts`. The QEMU
invocation routes COM1 to `/tmp/soupos-serial.log`. A second polled UART
on **COM2** (`serial2_*`, 0x2F8) carries the AI bridge; it is probed via
the 16550 scratch register so it is a no-op when QEMU has no COM2 backend.

## AI serial bridge (`src/ai.c`, `ai <prompt>`)

soupOS can't host an LLM, so `ai` talks over COM2 to a host daemon. Protocol:
kernel sends `"<prompt>\n"`; host streams the reply and ends it with `0x04`
(EOT). `ai_getc` yields to the scheduler between polls (so `bgclock` etc. keep
running) and times out if no daemon answers — `ai_available()` (the scratch
probe) gates the shell command so a missing bridge prints a message instead of
hanging. The shell mirrors the assembled reply to klog/COM1 (see `dmesg`).
There's also a soupyc `ai(prompt)` builtin (reply capped at the 47-char string
limit). Host side: `tools/ai_bridge.py` (Ollama by default, `AI_FAKE=1` for
offline canned replies) and `make run-ai`, which wires COM2 to a unix socket
and launches the daemon. Real replies need an LLM — the homelab Ollama is at
`localhost:11434`.

### Kernel log ring (`src/klog.c`)
- `KLOG_SIZE = 8192`-byte static ring in BSS — usable from the first
  boot step and from interrupt context (no heap).
- `klog(fmt, ...)` is a printf subset (`%s %u %d %x %p %c %%`);
  `klog_puts`/`klog_putc` for plain text. **Every byte is mirrored to
  COM1**, so the serial capture and the in-RAM ring always agree.
- Boot messages (`[boot] ...`) route through klog. `klog_copy()` drains
  it oldest→newest; the shell `dmesg` command dumps it.

### Panic (`src/isr.c` → `isr_handler`)
On a CPU exception (vectors 0–31) the handler dumps to the VGA panic
screen **and** mirrors the full report to klog/serial:
- exception name + number, error code, `eip/cs/eflags`
- all GP registers; `esp` is reconstructed as `&regs->eflags + 4`
  (ring 0 → no SS:ESP pushed)
- page faults: `CR2` + decoded cause (present/write/user bits)
- an EBP frame walk (`stack_trace`) printing return addresses.
  `frame_ok()` bounds each frame to 4-aligned, identity-mapped low
  128 MB so the walk itself cannot fault.

**The build uses `-fno-omit-frame-pointer`** — without it `-O2` reuses
EBP as a scratch register and the stack walk finds nothing.

---

## User mode: ring 3, syscalls, ELF (v0.7.0)

soupOS runs real ring-3 user programs with hardware memory protection.

- **GDT/TSS** (`gdt.c`): user code/data selectors `SEL_UCODE 0x1B` / `SEL_UDATA
  0x23`, and a TSS (`SEL_TSS 0x28`) holding `ss0`/`esp0` — the dedicated 16 KB
  kernel stack the CPU switches to on a ring3→ring0 trap. `ltr` at boot.
- **Paging** (`paging.c`): `map_page` ORs the user bit into the PDE when a user
  PTE is added; kernel PTEs stay `U=0` so kernel memory is unreachable from
  ring 3 (privilege is AND-ed PDE&PTE). `paging_unmap` frees a mapping.
- **Syscalls**: `int 0x80`, DPL-3 gate → `isr128` stub → `syscall_dispatch`
  (`usermode.c`). eax=number, ebx/ecx/edx=args, eax=return. Numbers in
  `syscall_nr.h` (9 calls):
  `SYS_EXIT/WRITE/READ/YIELD/OPEN/CLOSE/SBRK/ARGS/TICKS`.
  - `write` fd 1=screen, 2=serial, fd≥3 → VFS; `read` fd 0=keyboard,
    fd≥3 → VFS.
  - `open(path, mode)` (0=read, 1=write+create) returns an fd from a
    per-run 16-slot table (`g_ufds`, fd≥3); `close(fd)` frees it.
  - `sbrk(incr)` grows/shrinks the user heap break (`g_user_brk`, base
    `USER_HEAP 0xC4000000`), mapping fresh user pages up to the stack;
    returns the old break (−1 on failure).
  - `args(buf, max)` copies the `cook` command-line tail (`g_user_args`,
    128 B) into the program.
  - `yield` (v0.7.2) re-enables interrupts and calls `task_yield()`, so a
    ring-3 program can cooperate with background kernel tasks. Safe because
    only one user program runs at a time, so the frame left on the TSS
    (esp0) stack can't be clobbered by another ring3→ring0 trap while
    switched away; iret restores the user's IF from its saved EFLAGS. Demo:
    shell `yieldbg` + `cook spin.elf` show interleaved serial markers.
  - `ticks` (v0.7.2) returns `timer_get_ticks()` (100 Hz uptime) — lets a
    user program pace itself in wall-clock time (see `user/spin.c`).
  - Every user pointer is bounds-checked via `user_ok()` so a buggy or
    hostile program can't make the kernel touch non-user memory.
- **Transitions** (`usermode_asm.asm`): `user_mode_enter(entry, ustack)` builds
  an `iret` frame into ring 3; the exit syscall calls `user_mode_exit`, which
  restores the saved kernel stack and returns out of `exec_elf` (one-shot
  coroutine swap — no per-process kernel thread yet).
- **ELF loader** (`exec_elf`): ELF32 ET_EXEC i386 only. Maps each PT_LOAD into
  fresh user pages at/above `USER_BASE 0xC0000000` (above the identity map, so
  no aliasing), copies the image, zeroes .bss, sets a 16 KB user stack at
  `USTACK_TOP 0xC8000000`, runs, tears the pages down on exit.
- **Single address space for now**: one user program at a time, mapped into the
  kernel page directory. Concurrency needs per-process page directories.
- Shell `cook <prog.elf> [args]`. User programs live in `user/` (freestanding,
  talk to the kernel only via `int 0x80`). They link the **user runtime**
  `user/ulib.{c,h}` — `_start` (calls `main`, exits with its return value),
  thin wrappers over every syscall, and conveniences (`print`/`eprint`/
  `print_int`, `strlen_`, a bump `malloc` over `sbrk`). A program is just
  `int main(void)`. `make user` builds five ELFs: `hello` (prints +
  exits), `echo` (prints args), `cat` (open+read+write a file), `systest`
  (exercises args/open/read/sbrk, emits `TESTOUT` markers to serial for
  headless verification), `spin` (loops on `ticks`+`yield` for ~3s to
  demonstrate cooperative scheduling from ring 3). Link them at `USER_BASE`
  with `user/user.ld`; the disk recipe mcopies HELLO/CAT/ECHO/SYSTEST/SPIN.ELF.

## Doom Port — Architecture

`src/doom.c` is the entire Doom port in one file (~2200 lines). It is organized in stages:

### Stage 3/4 — Title + Menu
- Reads `PLAYPAL` lump → loads 256-color palette into VGA DAC
- Draws `TITLEPIC` as the background; animated skull cursor menu
- Menu state machine: Main → Episode → Skill → `doom_play_level()`

### Stage 5 — Automap
- Loads level lumps (THINGS, LINEDEFS, SIDEDEFS, VERTEXES, SEGS, SSECTORS, NODES, SECTORS) relative to the `ExMy` marker lump
- Draws all linedefs as lines; player dot + direction arrow
- TAB key toggles between automap and 3D view

### Stage 6/7/8 — 3D Renderer

#### Coordinate system
- Map coordinates: `int16_t` x/y in Doom map units (1 unit ≈ 1 inch)
- Player position: `pl_x`, `pl_y` stored as **×256 fixed-point**; map units = `pl_x >> 8`
- Player eye height: `pl_eye_z = sector.floor_h + 41` (updated each frame via `find_sector_at`)

#### View angle
- `pl_angle`: 0–31 (32 steps, 11.25° each); 0 = East, 8 = North
- `view_cos[32]` / `view_sin[32]`: ×1024 fixed-point trig tables
- Doom angle (degrees) → `pl_angle`: `th[i].angle * 32 / 360`

#### Projection
```
PROJ_DIST = 160   (half screen width; gives 90° FOV)
VIEW_H    = 168   (3D viewport height; rows 168–199 = status bar)
VIEW_HALF_H = 84  (horizon line)

view_project(wx, wy) → vx (forward ×1024), vy (rightward ×1024)
  vx = cos[angle]*(wx-px) + sin[angle]*(wy-py)
  vy = sin[angle]*(wx-px) - cos[angle]*(wy-py)

screen_x = 160 + vy * 160 / vx
screen_y = 84  - h_rel * 160 * 1024 / vx   (h_rel = world_z - pl_eye_z)
```

#### BSP traversal
- `bsp_traverse(node_id)` — front-to-back, root = `lv_nnodes - 1`
- Leaf nodes: bit 15 set, low 15 bits = ssector index
- Side test: `s = nd->dy*(px-nd->x) - nd->dx*(py-nd->y)`
  - `s >= 0` → player on RIGHT = child[0] = front; visit child[0] first
  - `s < 0` → player on LEFT = child[1] = front; visit child[1] first
- Per-column occlusion: `col_top[320]` / `col_bot[320]` track open vertical span for walls
- `spr_top[320]` / `spr_bot[320]` — separate span arrays for sprites (updated only by portal steps, not solid walls)
- `cols_open` counter — BSP stops when all columns are closed

#### Wall rendering (`render_seg_3d`)
- Near-plane clip: `NP = 256` view-units; segs with both vertices behind clipped
- `wall_h = proj_y(floor) - proj_y(ceil)` — full sector height in screen pixels
- Our V-math anchors V=0 at screen y=yfc (front ceiling). Pegging is done by
  shifting `v_yoff` so that V=0 lands at the correct world Z for each case.
- **Pegging flags**: `LD_UPPER_UNPEGGED = 0x0008`, `LD_LOWER_UNPEGGED = 0x0010`
- **Mid texture (solid wall)**
  - Default: V=0 at front ceiling (no shift).
  - `LOWER_UNPEGGED`: V=0 at front floor → `v_yoff += tex->height - wall_h`.
- **Upper texture (portal step: front ceiling → back ceiling)**
  - Default: texture bottom aligned with back ceiling → `v_yoff += (fc - bc) - tex->height`.
  - `UPPER_UNPEGGED`: V=0 at front ceiling (no shift). This is the usual door-top behavior.
- **Lower texture (portal step: back floor → front floor)**
  - Default: V=0 at back floor (top of step) → `v_yoff += (fc - bf)`.
  - `LOWER_UNPEGGED`: V=0 at front ceiling (no shift). Used when the lower texture
    should visually continue from the ceiling texture above.
- Two-sided segs render upper step (yfc→ybc-1), lower step (ybf+1→yff), and
  narrow the per-column span (`col_top`/`col_bot` and `spr_top`/`spr_bot`).

#### Texture system
- `MAX_TEX = 64` cached textures (on-heap pixel arrays)
- `tex_get(name)` — searches TEXTURE1/TEXTURE2 lump, composites patches
- Texture compositing via `patch_composite()`: uses patch's `originx/originy` WITHOUT subtracting `leftoffset/topoffset` (correct Doom behavior)
- `col_fill_tex()`: fixed-point V stepping (`v_step = tex_height*256/wall_h`) to avoid per-pixel division
- **COLORMAP** — 34×256-byte lump loaded at level start. `col_fill_tex`, `draw_sprites`, and flat raycaster pick shade index from forward distance for diminished lighting.

#### Flat (floor/ceiling) system
- `flat_get(name)` — per-sector 64×64 flat cache, up to `MAX_FLAT_CACHE = 32` entries, heap-allocated
- `draw_flats(psec)` — per-row raycaster that calls `find_sector_at()` **per column** (with a last-sector cache so BSP walks only happen at sector transitions). Correct flat and sky are sampled across sector boundaries within a single row. Note: the raycast depth `d` is still derived from the player's sector heights — flats in adjacent sectors of different height are slightly stretched. True per-sector height handling requires visplanes.
- **Sky**: sectors with `F_SKY1` ceiling draw the `SKY1` patch (256×128) per-column inline in the flat raycaster, U-scrolled by view angle (`pl_angle * 8 + sx * 64 / VGA13_W`), V mapped from row to sky height

#### Double buffering
- `g_backbuf[320*200]` — all rendering targets this static array
- `present_frame()` — waits for VGA vertical blank (port 0x3DA bit 3), then blasts buffer to 0xA0000
- `draw_patch()`, `col_fill()`, `col_fill_tex()`, `bb_fill_rect()` all write to `g_backbuf`

#### Collision detection
- `try_move(dx, dy)` — circle-vs-segment distance check against all linedefs; wall-slides by trying X-only then Y-only
- `hits_any_ld(nx, ny)` — blocks if distance from new position to any linedef < `PLAYER_RADIUS = 16`
- `ld_blocks(i)` — one-sided OR `LD_BLOCKING (0x0001)` OR portal opening gap < 56 mu (door closed)
- After successful move, calls `walk_trigger(ox, oy, nx, ny)` to fire walk-activated linedefs

#### Movement controls
- W/↑ forward, S/↓ back, A strafe-left, D strafe-right, ←/→ turn
- TAB toggle automap/3D, ESC back to menu
- **Space = USE** (doors, lifts), **Ctrl = fire pistol**
- `MOVE_DELAY = 1` tick, `TURN_DELAY = 4` ticks, `USE_DELAY = 20` ticks

#### Door / lift system
- **Doors** — `door_t` array (max 16). `doors_tick()` animates `ceil_h` each frame at `DOOR_SPEED = 2` mu/tick. DR doors auto-close after `DOOR_WAIT = 150` ticks. D1 doors remove themselves when fully open.
  - USE specials handled: 1, 26, 27, 28, 29, 31, 32, 33, 34, 46, 61, 63, 103, 117, 118
  - Walk specials: 2 (W1 open), 86 (WR open)
- **Lifts** — `lift_t` array (max 16). `lifts_tick()` animates `floor_h` at `LIFT_SPEED = 4` mu/tick. Waits `LIFT_WAIT = 105` ticks at bottom.
  - Walk specials: 10 (W1), 88 (WR)
  - USE specials: 21 (S1), 62 (SR)

#### Enemy AI
- `thing_ai_t` parallel array alongside `lv_things`. Fields: `state`, `atk_cd`, `hp`
- States: `AI_IDLE (0)` → `AI_CHASE (1)` when player within 512 mu → `AI_DEAD (2)` when HP ≤ 0
- Chase: moves 4 mu/frame toward player. Melee: 10 damage within 72 mu, 50-tick cooldown
- HP per type: Zombieman=20, Shotgun Guy=30, Imp=60, Demon=150, Baron=1000, Cyberdemon=4000
- Dead monsters play 6-frame death animation (frames N→S, 8 ticks each), then `type = 0`

#### Sprite renderer (three-phase vissprite pipeline)
- `build_vissprites()` — called before BSP. Projects each thing, resolves
  frame letter + rotation, computes screen-space params (`sx_left/right`,
  `sty`, `screen_h`, `cmap`), records the thing's **subsector id** via
  `find_subsector_at`, leaves `snapshot_valid = 0`.
- `snap_vissprites(ss)` — called from `bsp_traverse` at each leaf **before**
  the subsector's segs are rendered. For every vissprite whose `ssec_id`
  matches and has no snapshot yet, copies `spr_top[]`/`spr_bot[]` into
  per-vissprite arrays. This freezes the sprite's clip spans to the state
  they had when its subsector was reached — fixes sprites being clipped by
  portals belonging to unrelated later subsectors.
- `draw_vissprites()` — called after BSP. Insertion-sorts far-to-near and
  renders using each sprite's snapshot. Vissprites whose subsector was never
  visited (e.g. all columns closed before reaching it) have
  `snapshot_valid = 0` and are skipped — correct, they are fully occluded.
- `MAX_VIS_SPR = 64`, static BSS arrays (each vissprite carries 320×2 int16
  snap buffers → ~82 KB total).
- Transparency: sprite cache is `memset(0xFF)`; `patch_blit` writes only the
  pixels covered by posts. Renderer skips `pix == 0xFF`, so real palette
  index 0 (black) now renders correctly (previous `if (pix)` dropped it).
- `MAX_SPRITES = 128` cached sprite lumps.
- Frame selection: idle='A', chase cycles 'A'..'D' every 10 ticks, melee flash='E', dead='N'+frame.
- Rotation: `angle_to_player_32()` computes 8-way facing for live monsters; dead/items use rotation '0'.
- Uses int64 for scale math to prevent truncation at distance.

#### Weapon overlay
- `PISGA0` loaded at level start into `g_pistol_raw`, drawn via `draw_patch(g_pistol_raw, 160, VIEW_H)` after sprites each frame

#### Status bar
- Drawn at `ST_Y = 168` using WAD lumps: `STBAR`, `STFST00`, `STTNUM0-9`, `STTPRCNT`
- Wired to live state: `pl_health`, `pl_armor`, `pl_ammo`

#### Pickups
- `try_pickups()` scans things within 32 mu each frame
- Health: bonus (2014), stimpack (2011), medikit (2012), soul sphere (2013)
- Armor: bonus (2018), mega armor (2019)
- Ammo: clip (2007), box of bullets (2048), shells (2008), box of shells (2049)
- Weapons: shotgun (2001), chaingun (2002) — grant ammo, picked up once

---

## WAD Structures (on-disk, little-endian)

All structs are packed to match WAD binary layout exactly:

```c
dvertex_t   { int16 x, y }                            // 4 B
dlinedef_t  { u16 v1,v2, flags, special, tag,
              right_sdef, left_sdef }                  // 14 B
dthing_t    { int16 x,y; u16 angle,type,flags }        // 10 B
dsector_t   { int16 floor_h, ceil_h; char floor_flat[8],
              ceil_flat[8]; int16 light; u16 special,tag } // 26 B
dsidedef_t  { int16 xoff, yoff; char upper[8],lower[8],
              mid[8]; u16 sector }                     // 30 B
dseg_t      { u16 v1,v2; int16 angle; u16 linedef,side;
              int16 offset }                           // 12 B
              // angle = BAM angle (unused); offset = texture U offset
dssector_t  { u16 numsegs, firstseg }                  // 4 B
dnode_t     { int16 x,y,dx,dy; int16 bbox[2][4];
              u16 child[2] }                           // 28 B
```

Map lump offsets from ExMy marker:
```
+1 THINGS  +2 LINEDEFS  +3 SIDEDEFS  +4 VERTEXES
+5 SEGS    +6 SSECTORS  +7 NODES     +8 SECTORS
```

---

## Key APIs

```c
// timer.h
uint32_t timer_get_ticks(void);   // 100 Hz ticks since boot

// keyboard.h
int  keyboard_key_pressed(uint8_t scancode);  // 1=held
int  keyboard_getchar(void);                  // blocks
void keyboard_flush(void);

// KEY_SC_* scancodes: W=0x11, S=0x1F, A=0x1E, D=0x20,
//   UP=0xC8, DOWN=0xD0, LEFT=0xCB, RIGHT=0xCD,
//   ESC=0x01, TAB=0x0F, SPACE=0x39, ENTER=0x1C, CTRL=0x1D

// heap.h
void *kmalloc(uint32_t size);
void  kfree(void *ptr);

// wad.h
int      wad_init(const char *filename);
int      wad_find_lump(const char *name);       // -1 if not found
uint32_t wad_lump_size(int idx);
int      wad_read_lump(int idx, void *buf, uint32_t max);
const wad_lump_t *wad_get_lump(int idx);        // pointer into index

// vga13h.h
void    vga13h_enter(void);        // switch to mode 13h
void    vga13h_exit(void);         // back to text mode
void    vga13h_setpal(u8 idx, u8 r, u8 g, u8 b);  // r/g/b are 0–63
uint8_t *vga13h_fb(void);          // returns 0xA0000
void    vga13h_clear(uint8_t c);
void    vga13h_fill_rect(int x, int y, int w, int h, uint8_t c);
void    vga13h_blit(int x, int y, int w, int h, const uint8_t *pixels);
```

---

## Feature Status

### ✅ Done
| Feature | Notes |
|---------|-------|
| Title screen + animated menu | Episode + skill selection |
| BSP 3D renderer | Front-to-back with column occlusion |
| Wall textures + pegging | TEXTURE1/2 compositing, upper/mid/lower |
| COLORMAP diminished lighting | Distance-based shade on walls, sprites, flats |
| Per-sector floor/ceiling flats | `find_sector_at` once per row; fixes height-transition glitch |
| Sky rendering | F_SKY1 → SKY1 patch, scrolls with view angle |
| Sprite billboard renderer | 8-way rotation, depth-sorted, `spr_top/spr_bot` clipping |
| Enemy AI | Idle/chase/dead states; melee; per-type HP |
| Enemy sprite animation | Walk cycle, attack flash, 6-frame death sequence |
| Hitscan pistol (Ctrl) | 20° cone, 1024 mu range, 15-tick cooldown |
| Pickups | Health, armor, ammo, weapons |
| Doors (USE + walk) | Animate ceil_h; DR auto-close; D1 stay-open |
| Lifts (USE + walk) | Animate floor_h; lower/wait/raise cycle |
| Weapon sprite overlay | PISGA0 drawn at viewport bottom each frame |
| Status bar | Wired to live health/armor/ammo |
| Player death | Returns to menu when health ≤ 0 |
| Automap | All linedefs + player dot/arrow; TAB toggle |
| Double buffering + vsync | Wait VBlank before blit |
| Level exit | Special 52 (W1 walk) sets `g_level_exit`; special 11 (S1 USE) sets it too. `doom_play_level` outer loop advances map, shows text-mode "E1Mx COMPLETE!" screen for ~1.5s, then loads next map. Health/ammo carry over; stats only reset on new game. |
| Enemy ranged attacks | `proj_t` array (`MAX_PROJS=32`) in `g_projs[]`. Imp (3001) spawns fireball via `spawn_proj()` when beyond melee range and cooldown=0; Zombieman/ShotgunGuy hitscan. `projs_tick()` moves projectiles, checks collision, deals damage. `projs_draw()` projects to screen and renders 3×3 dot at `PROJ_COL=176`. Called from `draw_3d`. |
| Weapon fire animation | `g_pistol_flash` holds `PISFB0` lump. `draw_weapon()` shows flash when `pl_fire_cd > FIRE_CD_TICKS - FLASH_TICKS` (5 frames), idle sprite otherwise. |

| Sector specials | `g_sec_base_light[]` stores original light. `sectors_tick(psec)`: specials 5/7/16 deal 10/5/20 HP/sec via `g_dmg_next` timer; specials 1/3/17 animate `lv_sectors[i].light` from base each frame (flicker/strobe/oscillate). Called from `draw_3d`. |
| Locked doors | `pl_keys` bitmask (KEY_BLUE=1, KEY_YELLOW=2, KEY_RED=4). Keys set in `try_pickups` (types 5/40=blue, 13/38=red, 6/39=yellow). `door_try_use` blocks specials 26/32 (blue), 27/34 (yellow), 28/33 (red) if player lacks key. Keys carry over between maps; reset on new game. |
| Player death screen | After inner game loop, `pl_health <= 0` detected. Switches to text mode, shows "YOU DIED." in red, waits for keypress, then returns to main menu. |

### ❌ Not yet done (ordered by impact)

1. **Sound** — PC speaker SFX for weapons, doors, monster alerts (DSPISTOL, DSDOOROP, DSPOSIT1, etc.). Deferred until gameplay is complete.

---

## Known Bugs / Stability Notes

- `find_sector_at` returns -1 if player escapes map geometry; `pl_eye_z` holds stale value (safe, incorrect height until player re-enters valid area).
- Heap is finite (~8 MB). `MAX_TEX = 64`, `MAX_SPRITES = 128`, `MAX_FLAT_CACHE = 32`. Loading a large map with many unique textures can exhaust the heap — `tex_shutdown` / `spr_shutdown` free everything at level transitions.
- Wall geometry one-pixel gaps at some angles are a near-plane clipping edge case; harmless.

---

## Common Pitfalls

1. **No libc**: use `kmalloc`/`kfree`, not `malloc`/`free`. Use functions from `doom_libc.h` / `str.h` for `memcpy`, `memset`, `strlen`, etc.
2. **32-bit only**: `int64_t` is fine for intermediate math (libgcc provides `__divdi3`). No float/double.
3. **I/O ports** need `inb`/`outb` inline asm — see `idt.c` or `ata.c` for the pattern. Do NOT memory-map I/O ports.
4. **VGA framebuffer** is at `0xA0000` (320×200 bytes). Write to `g_backbuf` and call `present_frame()` at frame end — direct writes to `vga13h_fb()` will flicker.
5. **WAD lump names** are 8 bytes, NOT null-terminated when all 8 bytes are used. Always copy to a `char safe[9]` before passing to `wad_find_lump`. Use `tex_name_eq()` for 8-byte comparison.
6. **Fixed-point math**: player position is ×256. View vectors are ×1024. Use int64 for intermediate products to avoid overflow — a prior bug truncated `PROJ_DIST*1024/vx` to 0 for distant sprites, making them invisible.
7. **BSP side test uses `s >= 0`** (not `s > 0`) for child[0]. Both `bsp_traverse` and `find_sector_at` must use the same convention — a prior bug with `s > 0` caused things exactly on splitters to land in invalid subsectors.
8. **`spr_top`/`spr_bot` vs `col_top`/`col_bot`**: solid walls close `col_top`/`col_bot` columns but must NOT close `spr_top`/`spr_bot` — only portal step updates flow into sprite spans. Mixing them causes sprites to vanish behind solid back walls. Sprites are still correctly hidden by solid walls because the vissprite pipeline **snapshots** `spr_top`/`spr_bot` at the time the sprite's own subsector is rendered — if that subsector is never reached (cols_open hit 0 first), the sprite is skipped entirely.
9. **Sprite transparency sentinel is 0xFF**. The sprite cache is filled with 0xFF before `patch_blit` decodes posts into it, and `draw_vissprites` skips `pix == 0xFF`. Never write 0xFF into a sprite bitmap as a real pixel — it will be invisible. (Doom's palette index 255 is rarely used by `DOOM1.WAD` sprites, so this is safe in practice.)
10. **Texture V wrap fast path**: `col_fill_tex` special-cases power-of-2 `tex->height` with `& (th-1)` and always advances the backbuffer pointer by `VGA13_W` per row (no per-pixel multiply). Almost all Doom textures are power-of-2 tall, so this path is hit for walls at all times.
11. **Any blocking wait loop must yield**: the cooperative scheduler can
    only run other tasks when someone calls `task_yield()`. A busy-wait
    on I/O (`while (!ready) {}`) will starve every other task — instead:
    `while (!ready) { task_yield(); if (!ready) __asm__("hlt"); }`. The
    `hlt` parks the CPU until the next IRQ (so you're not burning cycles
    when no one else wants to run). `task_yield` is a safe no-op before
    `task_init()`, so drivers can use this pattern unconditionally.
12. **Don't reap the currently running task**: `task_kill` refuses to kill
    `task_current()` and task 0, because freeing the stack of a task that
    is mid-execution would immediately corrupt its own locals. Tasks exit
    themselves by returning from their entry function (which routes to
    `task_exit` via the seeded stack frame).

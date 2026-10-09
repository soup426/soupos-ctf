# soupOS — Developer Context for AI Agents

## What this is

soupOS is a 32-bit x86 hobby OS written in C and NASM assembly (currently
**v0.60.154**; ROADMAP.txt is the authority on the version). It boots via GRUB and runs in protected mode with paging, a
physical/heap allocator, and a cooperative scheduler. On top of that:

- **Filesystem**: FAT16 read/write with subdirectories ("bowls") and a VFS shim
  over files + `/dev` nodes. File commands incl. `decant` (cp) / `relabel` (mv).
- **Multi-user**: accounts ("kitchen staff") in `/etc/kitchen`, a login gate,
  and advisory rwx/owner permissions stamped into FAT dirents.
- **Shell + tools**: line-editing shell, `jot` full-screen text editor, and a
  `soupyc` scripting language (functions, arrays, file I/O, `include`, builtins,
  triple-quoted multi-line strings, line-numbered errors; strings cap at 47 ch).
- **`ai <prompt>`**: a COM2 serial bridge to a host-side LLM (`make run-ai`).
- **Ring-3 processes (v0.7.0, concurrent since v0.8.0)**: `cook <prog.elf>`
  loads and runs ELF32 programs in ring 3 over an `int 0x80` syscall interface,
  with real (U/S-bit) memory protection and a private address space each.
  Several run at once: `cook prog &` backgrounds, `orders` lists them, `kill` and
  Ctrl-C end them. See "User mode" and "Processes" below.

Theming note: commands are kitchen-flavoured (`pour`/`stir`/`serve`/`cook`/…).
**Keep it up when adding commands.** Networking is `faucet`/`plumbing`/`table`/
`sip`/`sniff`, jobs are `orders`/`plate`/`steep`, the clipboard is `scraps`.

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

# Build a ring-3 user program (user/hello.c -> user/greet.elf)
make user

# Rebuild FAT disk image (wipes disk.img; includes the .SC demos + HELLO.ELF)
make distclean && make disk

# Add DOOM1.WAD to existing disk image
make wad
```

**Headless smoke test** (the oracle — use this as the gate after any change):

**Run the full check on the test container, not the laptop** (2026-10-09, at
the user's request: ~200 QEMUs at once made the laptop unusable).
`scripts/remote-check.sh` copies the tree to CT 123 `soupos-test` on atlas
(Arch, 16 cores, 8 GB, /dev/kvm) through `ssh atlas pct exec 123` and runs
check.sh there (~6 min); the laptop only copies. check.sh itself now runs at
most CHECK_JOBS pieces at once (default twice the cores). Single test
scripts while working on something are fine here.
```bash
./scripts/remote-check.sh        # THE pre-commit check, on CT 123 (pct start 123 if down)
./scripts/check.sh               # the same, here: five configs + gate + every
                                 # test script, all in parallel (~the slowest piece)
./scripts/check.sh quick         # configs + gate only
./scripts/smoke-test.sh          # the gate alone: eight segments under scripts/gate/,
                                 # each its own headless boot, all at once (~100 s)
./scripts/gate/jobs.sh           # one segment by itself while working on it
KEEP=1 ./scripts/smoke-test.sh   # keep every segment's serial log for inspection
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
  check.sh          Everything before a commit, in parallel: configs in tree
                    copies, the gate and the side scripts each on their own QEMU
  smoke-test.sh     The gate: hands over to gate.sh
  gate.sh           Runs every segment under gate/ in parallel, one verdict
  gate/lib.sh       Boot, drive, check and verdict shared by the segments
  gate/<seg>.sh     boot, jobs, fat, vga, desk, net, soupyc, audio
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
  0x23`, and a TSS (`SEL_TSS 0x28`) holding `ss0`/`esp0` — the kernel stack the
  CPU switches to on a ring3→ring0 trap. `ltr` at boot.
  **`esp0` is per-task state (v0.8.0)**: it is `task_t.user_resume_esp`, the
  kernel esp `user_mode_enter` captured after its own pushes, and `task_yield`
  moves it with `current` on every switch. With two ring-3 programs alive a
  single `esp0` is a corruption bug: the second program to trap pushes its
  frame onto the stack where the first one's suspended frame still lives. It is
  the resume esp rather than the stack top because `user_mode_enter` leaves
  four callee-saved registers on that stack for as long as ring 3 runs.
  `tss_boot_esp0()` is the fallback for a task that has never entered ring 3.
- **Paging** (`paging.c`): `map_page` ORs the user bit into the PDE when a user
  PTE is added; kernel PTEs stay `U=0` so kernel memory is unreachable from
  ring 3 (privilege is AND-ed PDE&PTE). `paging_unmap` frees a mapping.
- **Stubs own the data segments (v0.8.3)**: every ISR/IRQ/syscall stub saves
  the interrupted context's `ds`, loads `SEL_KDATA` for the handler, and
  restores it before `iret`; `registers_t.ds` is that saved value. Do not
  "simplify" this away. Segment registers are global state that `task_switch`
  does not save, so without it a task that blocks inside a syscall can resume
  after another task left `ds` set to the kernel selector, `iret` to ring 3
  holding a DPL-0 data segment, and have the CPU null every data segment.
  The program then faults, the fault handler faults identically because `ds`
  is null, and the machine dies with interrupts off and nothing logged.
- **Syscalls**: `int 0x80`, DPL-3 gate → `isr128` stub → `syscall_dispatch`
  (`usermode.c`). eax=number, ebx/ecx/edx=args, eax=return. Numbers in
  `syscall_nr.h` (9 calls):
  `SYS_EXIT/WRITE/READ/YIELD/OPEN/CLOSE/SBRK/ARGS/TICKS`.
  - `write` fd 1=screen, 2=serial, fd≥3 → VFS; `read` fd 0=keyboard,
    fd≥3 → VFS.
  - `read` fd 0 only reaches the keyboard for the **foreground** process; any
    other process gets 0 bytes (EOF) rather than stealing the user's keys. The
    wait is interruptible, so a killed program sitting in `read` notices, and
    it enables interrupts first (the gate is an interrupt gate, so waiting
    with IF clear would wait forever for the IRQ that fills the buffer).
  - `open(path, mode)` (0=read, 1=write+create) returns an fd from the
    process's own 16-slot table (`proc_t.ufds`, fd≥3); `close(fd)` frees it.
  - `sbrk(incr)` grows/shrinks that process's heap break (`proc_t.brk`, base
    `USER_HEAP 0xC4000000`), mapping fresh user pages up to the stack;
    returns the old break (−1 on failure).
  - `args(buf, max)` copies the `cook` command-line tail (`proc_t.args`,
    128 B) into the program.
  - `yield` (v0.7.2) re-enables interrupts and calls `task_yield()`, so a
    ring-3 program can cooperate with other tasks. This used to be safe only
    because one user program ran at a time (nothing else could clobber the
    frame left on the shared esp0 stack); since v0.8.0 it is safe by
    construction, because `esp0` follows `current` and every task traps onto
    its own kernel stack. iret restores the user's IF from its saved EFLAGS.
    Demo: shell `yieldbg` + `cook whisk.elf` show interleaved serial markers.
  - `ticks` (v0.7.2) returns `timer_get_ticks()` (100 Hz uptime) — lets a
    user program pace itself in wall-clock time (see `user/spin.c`).
  - Every user pointer is bounds-checked via `user_ok()` so a buggy or
    hostile program can't make the kernel touch non-user memory. Since
    v0.60.149 it also checks every page: mapped, or the heap below the
    break, or the stack above its guard. A range check alone let the
    kernel's own copy fault on a page the program never had, and isr.c
    panics on that; prod.elf and prod-test.sh keep it closed.
- **Transitions** (`usermode_asm.asm`): `user_mode_enter(entry, ustack)` records
  the resume esp on the current task (`usermode_arm_resume`, which also loads
  `tss.esp0`) and builds an `iret` frame into ring 3. `user_mode_exit` reads
  that task's resume esp back and returns out of `usermode_run`. It is reached
  three ways: the exit syscall, the fault handler, and the kill unwind.
- **ELF loader** (`usermode_run`, was `exec_elf`): ELF32 ET_EXEC i386 only.
  Takes the `proc_t`, runs on that process's own task. Maps each PT_LOAD into
  fresh user pages at/above `USER_BASE 0xC0000000` (above the identity map, so
  no aliasing), copies the image, zeroes .bss, sets a 16 KB user stack at
  `USTACK_TOP 0xC8000000`, runs, tears the pages down on exit.
- **Private address space per process**: `paging_new_dir()` per program,
  recorded on the task so the scheduler restores CR3 when the process is
  resumed, freed by `paging_free_dir` on exit. Page dedup asks
  `paging_is_mapped()` rather than a side table; `proc_t.upages` is only the
  per-process budget (`PROC_PAGE_CAP`, 4 MB).
- Shell `cook <prog.elf> [args]`. User programs live in `user/` (freestanding,
  talk to the kernel only via `int 0x80`). They link the **user runtime**
  `user/ulib.{c,h}` — `_start` (calls `main`, exits with its return value),
  thin wrappers over every syscall, and conveniences (`print`/`eprint`/
  `print_int`, `strlen_`, a bump `malloc` over `sbrk`). A program is just
  `int main(void)`. `make user` builds nine ELFs: `hello` (prints +
  exits), `echo` (prints args), `cat` (open+read+write a file), `systest`
  (exercises args/open/read/sbrk, emits `TESTOUT` markers to serial for
  headless verification), `spin` (loops on `ticks`+`yield` for ~3s to
  demonstrate cooperative scheduling from ring 3), `crash` (faults on purpose),
  `unhex` (hex stdin to a file), `marker` (tags every step with its argument so two
  copies prove concurrency) and `hog` (loops forever making NO syscalls, so
  only the IRQ kill path can end it). Link them at `USER_BASE` with
  `user/user.ld`; the disk recipe mcopies each one.

## Windows for programs (v0.60.146)

`SYS_WIN_OPEN/PUT/EVENT/CLOSE` (16-19, `syscall_nr.h`, `uwinev_t`) give a
ring-3 program a window on countertop; `user/frost.elf` (paint) is the
first user, `user/swirl.elf` (Mandelbrot, 16.16 fixed point, checked pixel
for pixel against Python by swirl-test.sh) the second. The calls run on the program's task and only touch a slot in
countertop.c's `app[]` under `app_mtx`; the desktop loop is the only
drawer (it shows new slots, repaints dirty ones, closes the windows of
programs that closed them or ended). PUT copies the program's whole body
into a kernel buffer (640x480 at most), so nothing is drawn from user
memory. The close box only sends `WEV_CLOSE`: the program decides. With
the desktop down every call gives -1 (but CLOSE, which frees the slot);
a second CLOSE of one window gives -1. prodwin-test.sh attacks all of
it through `prod.elf limits`/`victim`. A program in a terminal window
writes its stderr there, not to serial, so frost-test reads frost's
`[frost]` lines from the terminal's cells.

## Networking (`src/rtl8139.c`, `src/net.c`, v0.9.0)

`sip 10.0.2.2` works. QEMU's user-mode network is the target, so addressing is
compiled in (10.0.2.15, gateway 10.0.2.2) rather than discovered by DHCP.

- **Driver**: RTL8139, all I/O ports plus two DMA areas. The buffers are static
  arrays because the kernel is identity-mapped, so their addresses are already
  physical. Transmit tracks completion with TOK/TUN/TABT, **not** the OWN bit,
  whose polarity is documented inconsistently and which silently wedged every
  descriptor when it was read the other way round.
- **`irq_unmask()`**: `idt_init` opens only IRQ0 and IRQ1, so any driver that
  wants interrupts must ask. A slave line (8-15) also needs the cascade, IRQ2,
  open on the master. Forgetting that looks exactly like a broken driver:
  transmits work, nothing is ever received.
- **Stack**: Ethernet, ARP (8-entry cache), IPv4 with checksums, ICMP echo in
  both directions. Host byte order above the wire, converted only in the header
  builders and parsers. Receive runs in the IRQ handler.
- **Verify with the packet capture, not with printf**: `make run NET_DUMP=1`
  writes every frame to `net.pcap`. Both bugs above were found that way, one of
  them (nothing transmitted) being invisible from inside the OS.
- **UDP and DNS (v0.9.1)**: UDP carries the pseudo-header checksum, and the
  stack keeps one outstanding request rather than a socket table. The resolver
  does A records only and passes a dotted quad straight through, so `sip` takes
  a name or an address by the same path. `ip_send` requires its next hop to be
  in the ARP cache and now logs when it drops a packet for want of one, which
  is how a silently-vanishing DNS query cost an hour.
- **DHCP (v0.9.2, `src/dhcp.c`)**: address, mask, gateway and resolver come
  from the lease; the compiled-in values are the fallback when nothing answers,
  and boot/`faucet` say which was used. **Arm the receiver before you send**:
  `net_udp_listen(port)` then send then `net_udp_wait`. SLIRP answers from
  inside the same process, so a reply can reach the interrupt handler before
  the send call returns, and anything arriving before the port is armed is
  dropped - which looks exactly like the server ignoring you.
- **TCP (v0.9.3, `src/tcp.c`)**: stage one only, the three-way handshake. One
  connection at a time. **A SYN occupies one sequence number** - `snd_nxt` is
  `iss + 1` after sending ours, `rcv_nxt` is their seq + 1 - and everything
  later depends on that being right. A duplicate SYN/ACK is re-acked, because
  the peer retransmits when our ACK is late. `tcp_close()` sends RST; the
  graceful FIN close is stage 2c.
- **Data (v0.9.4)** is stop-and-wait: one segment in flight, acked before the
  next. Sequence comparisons use `(int32_t)(a - b) > 0`, never a plain `>`,
  because the sequence space wraps. The receive buffer is written by the
  interrupt handler and drained with interrupts off.
- **Close (v0.9.5)**: full FIN state machine, active and passive. `tcp_input`
  must **not** early-return on anything but CLOSED/SYN_SENT - acknowledgements
  keep arriving after a FIN, and going deaf there made us retransmit data the
  peer had already answered. A close that times out waiting for the peer's FIN
  sends RST rather than silently forgetting the connection, which would leave
  it half-open at the other end. TIME_WAIT is 200 ms, not 2*MSL, on purpose.
- **QEMU quirk**: three connections to the same SLIRP port leave the third
  unanswered. Each works alone, the frames are identical; it is QEMU-side state
  outliving our close. The gate uses a separate listener port per check.
- Shell: `faucet`, `plumbing [ip|dhcp]`, `table`, `sip <host> [count]`,
  `sniff <host>`, `reserve <host> <port>`.

## Do not use memmove on the frame buffer (v0.10.3)

`str.c`'s `memmove` is a **byte** loop, so using it for a block move of 16-bit
VGA cells doubles the work. `vga.c`'s `scroll()` copies 32-bit words (two cells
per store) and is about twice as fast as the original cell-at-a-time loop;
`memmove` measured 47% slower than that original. `sample` guards the row
movement. The same caution applies anywhere a wide copy looks tempting.

## Volume, and the mixer's latency (v0.19.0)

`hush [0-100]` is the master level, applied to the **sum** before saturation,
so turning down removes clipping and keeps the music/effects balance.

**A voice added while playback is under way is first heard up to 63 ms later**
(the mixer's three-chunk lead on the card's read position). Whether two sounds
started in one command align exactly is a race against the second lump's WAD
read. `mixer-test.sh` searches offsets rather than assuming zero latency - if
it ever fails, check the reported offset before suspecting the mixer.

## Warnings are errors (v0.18.2)

Builds with `-Werror` plus `-Wshadow -Wpointer-arith -Wstrict-prototypes
-Wold-style-definition` on top of `-Wall -Wextra`. **`WERROR=0` turns it off**
for bisecting or a newer GCC. If a warning appears, fix it rather than
silencing it: the one tolerated warning in this tree trained the eye to skip
compiler output for a whole session.

## CPU accounting (v0.18.1)

`ps` shows a CPU% column: the timer charges each tick to the running task, and
the 100-tick window makes the figure a percentage directly. **It measures who
held the CPU, not who did work** - a task halted at a prompt still counts, so
the shell reads 100% when idle. Use it to ask whether a background service is
costing too much, which is what `sample` asserts (no service above 50%).
Measured idle: fbcon 0%.

## The boot logo (v0.18.0)

`assets/logo.txt` is the source of truth; `scripts/gen-logo.py` generates
`src/logo.h` (committed, so Python is not needed to build). **Never put UTF-8
in a C string meant for the screen** - VGA prints one glyph per byte, so each
block becomes three garbage characters. The screen gets CP437 via
`vga_puts_screen_only` (no serial mirror); the log gets UTF-8 via `klog`.

## The framebuffer console is 128x48 (v0.18.0)

Set in `fb_early` from the framebuffer size, **before the first character is
printed**: the cell store's row stride is the column count, so resizing later
re-interprets text already written. A text boot stays 80x25, where the boot log
scrolls the logo off - that is known and accepted, not a bug to fix by making
the logo smaller.

**Tests that place the mouse must not assume a screen size.** The pointer
starts at the console's centre. Drive it into a corner, where it clamps, then
move a known amount.

## Music (`src/music.c`, v0.17.0)

`hum <LUMP>` plays a MUS tune; a Doom level plays its own. **Music is not a
mixer voice** - sixteen channels do not fit in four, and it must not be stolen
by an effect - so the mixer asks for one sample per frame and adds it. Square
waves only; instruments, pitch bend and percussion (channel 15) are ignored on
purpose. Needs the WAD, so it is in the `DOOM=0` filter and the mixer stubs it
out under `NO_DOOM`.

## Audio timing has to be measured in isolation (v0.17.0)

QEMU's wav backend writes **only while the card is active**, so several sounds
played in one session land contiguously with no silence between them, and any
span-based measurement sweeps in whatever played next (a 513 ms effect read as
4251 ms). Fine-grained timing lives in `scripts/sound-test.sh`, which plays
exactly two things. The main gate checks only what the serial log can answer.

## Long filenames, write side (v0.20.0)

`fat_write` generates an 8.3 alias (spaces and dots dropped, `~N` checked
against a real lookup) and writes the LFN chain into a **run of consecutive
slots** reserved by `dir_alloc_run`; without a long enough run it falls back to
an 8.3-only name rather than failing. `scripts/fat-longname-test.sh` has
**mtools** read the image, which is the only evidence that is not
self-referential.

**soupOS's permission byte collides with VFAT's case flags.** Byte 12 holds
"base/extension is lower case" (0x08/0x10) in VFAT and `FAT_PERM_OX`/`OW` in
soupOS, so other tools show our aliases lower case. Cosmetic; the long name is
authoritative.

## Long filenames (v0.16.0, read side)

`fat_entry_t` carries both `name` (8.3) and `lfn`; use `fat_display_name()` to
show one. Either name opens a file. **The LFN checksum is validated against the
8.3 name** - do not skip it, or an orphaned chain attaches to the wrong entry.

**A path component can be a long name**, so its buffer is `FAT_LFN_MAX`, not
`FAT_NAME_MAX`. That cap bit twice: `next_component` (fat.c) and
`normalize_path` (shell.c). **If a file lists but will not open, the name is
being truncated on the way in** - look for a 13-byte component buffer before
suspecting the directory parser.

Writing long names is not implemented: a new file still gets a mangled 8.3 name.

## The vault: SSH transport (v0.29.0)

`ssh.c` is the server; `vault <port>` opens it. One session, static buffers,
4 KB packet cap. The chacha20-poly1305@openssh.com framing (two keys, length
encrypted separately, seq as nonce) lives in `write_packet`/`read_packet`; the
host key seed is `/HOSTKEY.ED`. Verify with `scripts/ssh-test.sh`, which runs
the real `ssh -vvv` and compares fingerprints. Password userauth against the roster, then one session channel bridged to the console through a 4 KB output ring (`ssh_sink` -> `flush_output`); `scripts/ssh-login.py` drives a real login with pexpect. TCP has a table of four listeners (`tcp_accept(port, ticks)`), so `pass`, `vault` and `hatch` coexist; `pass` and `hatch` build without Doom.

## Kill wakes a blocked task (v0.27.0)

`proc_kill` calls `task_unblock` on a target parked on a wait queue
(`task_t.blocked_on`), so a process blocked in a pipe read dies instead of
sitting there as the "newest process" forever. Every block site loops and
re-checks, so early wakes are safe; keep it that way. The Makefile now tracks
header dependencies (`-MMD`); before that, a header edit left stale objects and
produced a kernel that hung on the first ring-3 trap.

## Crypto primitives (v0.23.0 onward)

Each primitive lands with published test vectors in `selftest.c` (expected
values from an independent implementation, never from the kernel's own). SHA-256
and HMAC are in `sha256.c`; `random.c` is the entropy pool (RDRAND + IRQ timing + RTC, folded through SHA-256), checked across boots by `scripts/random-test.sh`; `chacha.c` is ChaCha20, Poly1305 and the RFC 8439 AEAD; `sha512.c` exists for Ed25519; `fe25519.h` shares the field between `curve25519.c` (X25519) and `ed25519.c` (RFC 8032 signatures); X25519 is in 16-bit limbs (the 1000-round RFC chain is `sample slow` / `scripts/x25519-test.sh`, not the gate). None of it is constant-time; say so wherever it is
advertised.

## The pass: a shell over TCP (v0.22.0)

`pass <port>` runs `remote.c` as a task that feeds socket bytes into
`console_rx_byte` and mirrors output through `console_set_sink`. The remote
user shares the one local shell, as a serial user does. **Clear text, no
authentication on the wire** - it is the step before SSH, which is the next
queue item. A test driving it must hold the `nc` session open between
commands, or EOF closes it before the accept poll runs.

## Several connections at once (v0.21.0)

`tcp.c` holds a table of four connections; **the listener owns none of them**,
so a SYN takes a free slot and the listener keeps listening. `tcp_accept`
returns a handle and `tcp_send_on`/`tcp_recv_on`/`tcp_close_on` act on it; the
plain `tcp_send`/`tcp_recv`/`tcp_close` forward to the slot `tcp_connect` took,
for the outbound commands.

**In a test script, never use a bare `wait`** - it waits for the QEMU process
too, which never exits. Collect the PIDs you care about and wait on those.

## Serving: the listening socket (v0.15.0)

`hatch [port]` answers HTTP GETs for files on the FAT volume;
`scripts/hatch-test.sh` fetches from the host through QEMU's hostfwd. TCP has
`tcp_listen`/`tcp_accept` with `TCP_LISTEN` and `TCP_SYN_RCVD`.

**The listener IS the connection** - one block of state - so a server must
listen again after each client, and a SYN mid-request is refused. A backlog
needs a connection table.

**To answer an unsolicited peer you must know its MAC without resolving**,
because replies are built in interrupt context where `ip_send` will not ARP.
`net_rx` therefore learns the sender's hardware address from every IP frame.
Do not remove that: a peer need not ARP us first (SLIRP learns our MAC from
DHCP), and without it every inbound handshake dies on "no arp entry".

## Profiling Doom (v0.14.1)

`scripts/doom-profile.sh` prints cycles per frame for both display modes. Two
things that make this hard to do by hand, both in the script's header: the
profiler reports every **50 frames** and only the in-level loop counts, so you
must drive **three returns** (main menu, episode, skill) to reach it; and `fps`
reads 50 in both modes because `FRAME_TICKS` caps it. Uncapped it measured
**1666 fps**, so there is roughly 33x headroom and the framebuffer scaler's
extra 35% per frame costs nothing.

## The framebuffer is the default (v0.14.0)

`make` builds the framebuffer kernel (FB=1); `make iso-text` builds the FB=0
text-mode one, which is the only way to get real mode 13h. Both consoles come
from the same cell store, so the serial mirror - and therefore the gate - is
identical either way.

**Mode 13h on a framebuffer boot**: `vga13h`'s pixel store is a pointer, like
vga.c's cell store. Use `vga13h_pixels()`, never `0xA0000`. Drawing goes to a
RAM shadow and `vga13h_present()` scales it 3x through a software palette copy;
call present where a frame ends. `vga13h_enter/exit` pause and resume `fbcon`,
because the console would otherwise paint over the picture.

**A program wanting the machine to itself** must ask `task_count_users()`, not
`task_count_alive()`: the console renderer and the mixer are permanent
services (`task_set_service()`) and would otherwise always look like rivals.

**Palette widening is `(v<<2)|(v>>4)`**, not `<<2`, or bright colours sit a
shade dark. Do not try to match QEMU bit-for-bit here; it has its own quirk,
and `scripts/doom-fb-test.sh` tolerates 4 levels and explains why.

## The sound mixer (`src/mixer.c`, v0.13.0)

Four voices summed with saturation into a cyclic AC97 ring (32 chunks of
~21 ms), topped up by a task holding a three-chunk lead - no interrupt, because
the 100 Hz scheduler tick is five times faster than a chunk empties. Lumps stay
raw and resample on the way out. `whistle` and `sizzle` are both voices now, so
there is one audio path; `sizzle` takes several lumps.

**Testing overlap: duration proves nothing.** A dropped voice and a queued
voice both leave the longer sound's length on the clock. Render both sources on
the host, sum them with the same saturation, and correlate envelopes - the mix
matches the sum (1.0000) over the louder source alone (0.87). That is what
`scripts/mixer-test.sh` does. Also: **the keystroke driver takes ~2 s per
command**, longer than most effects, so two typed commands never overlap - put
both sounds in one command.

## Every test script boots a copy of disk.img (v0.13.0)

An interactive QEMU window write-locks the image, so a test sharing it fails
with "Failed to get write lock". The copy also keeps runs hermetic.

**Never pipe into `grep -q` in a test (v0.60.20).** Every test runs under
pipefail; grep -q exits at its first match, the writer dies of SIGPIPE, and
the pipeline fails though it matched (a negated one passes when it should
fail). Pipe into `grep ... >/dev/null`. scripts/lint-test.sh enforces it.

## The framebuffer console (`src/fbcon.c`, v0.12.1)

A task diffs the 80x25 cell store against a shadow ~30x/s and repaints changed
glyphs (embedded 8x16 font, `src/font8x16.h`). **The cell store is a pointer**:
`vga_cells()` / `vga_set_backing()` in vga.c. On a framebuffer boot the legacy
0xB8000 window is DEAD (writes discarded, reads all-ones), so `fb_early()`
switches the store to a RAM array before the first character prints. Anything
poking cells directly must use `vga_cells()`, never a 0xB8000 literal. Mode 13h
commands decline on FB boots via `no_mode13h()`.

## The framebuffer (`src/fb.c`, v0.12.0)

`make FB=1` requests a linear framebuffer in the multiboot header; `make
iso-fb` builds that image, and `scripts/framebuffer-test.sh` verifies it by
screendump. **The header's video request OVERRIDES grub.cfg's gfxpayload** -
the opposite of the obvious assumption - so the request must never be in the
default build or the text console (which the gate drives) comes up in graphics
mode with nowhere to draw. Pitch is bytes per scanline, not width*4: a row
starts at `y * pitch`. The framebuffer lives at ~0xFD000000 and must be
`paging_map`ped before the first write.

## Adding a file that needs the WAD (v0.11.2)

`DOOM=0` filters `doom.c`, `wad.c` and `doomsnd.c` out of `SRC_C`, so anything
calling the WAD reader must be in that filter AND its shell command must sit
behind `#ifndef NO_DOOM` - the command body, the dispatch line, the name in the
completion list, and the help text. Five configurations have to build: default,
`FB=0`, `DOOM=0`, `CHALLENGE=1 DOOM=0`, `PROFILE=1`; `scripts/check.sh` builds
them all in parallel. **Make that check gate the
commit** rather than just precede it; a `;` instead of `&&` once let a broken
`DOOM=0` through.

## Doom sound effects (`src/doomsnd.c`, v0.11.1)

DS* lumps are DMX format: 8-byte header, then **unsigned** 8-bit samples
centred on 128, at 11025 Hz. `doomsnd_play("DSPISTOL")` recentres, widens and
resamples to the card's rate, non-blocking. `sizzle <LUMP>` plays any effect
from the shell, which is how to test without driving the game. One sound at a
time: the hardware has a single PCM-out channel and nothing mixes yet.

**Port numbers are 16-bit.** `outb_((uint8_t)port, ...)` compiles happily and
sends the byte to a completely different port; it cost an hour here, because the
first sound after boot worked and only later ones were silent. When a device
looks half-alive, read its registers back before theorising.

## AC97 sound (`src/ac97.c`, v0.11.0)

`whistle <hz> <ms>` plays a tone; `beep` is still the PC speaker. The card
walks a **buffer descriptor list** itself, unlike the RTL8139's one-buffer-per-
packet. Three things to keep in mind:

- **A descriptor's length is a count of SAMPLES**, not bytes and not frames.
  Wrong here plays the right sound at the wrong speed.
- **Volume is attenuation**: 0 is loudest, 0x8000 is mute.
- The BDL and sample buffers are read by the card, so they need physical
  addresses. Static storage works because the kernel is identity-mapped, the
  same reason the RTL8139 driver uses static buffers.

**Verify audio from the captured samples, never by ear.** The gate runs QEMU
with `-audiodev wav` and measures the tone's frequency on the host. A phase-step
error of 256x produced a 1.7 Hz wave at full amplitude, which is inaudible and
would have passed any listening test.

## soupyc loop scoping (fixed v0.10.5)

Both `N_FOR` and `N_WHILE` drop the bindings their body made, once per
iteration, so a `let` inside a loop no longer accumulates toward `MAX_VARS`
(64). `N_WHILE` was missing this and died at 64 passes with "too many
variables". If you add another looping construct, do the same: record `nvar`
before the loop and restore it after each pass.

## Verifying the FAT cluster leak (v0.10.3, proven v0.10.4)

`scripts/fullness-test.sh` builds a nearly-full image and copies a file that
cannot fit. It is not in the main gate because it needs its own disk. With the
leak it reports 22 KB free then 0 KB; with the fix, 22 KB both times. If you
touch `fat_write`'s failure paths, run it.

## FAT free space (v0.10.3)

`fat_space()` reports free/total clusters; `larder` is the command. Before this
there was no way to see free space from inside soupOS, which is how a cluster
leak sat unnoticed in `fat_write`: its failure paths returned with clusters
allocated and no directory entry referencing them. **If you add a failure path
to `fat_write`, send it to `fail:`**, which gives the chain back, except after
the directory entry is written, where freeing would leave a dangling entry.

## soupyc spawn (v0.10.2)

`spawn("fn")` runs a no-argument function on its own task and returns the task
id (`ps` lists it, `kill` takes it). The child gets an **independent copy** of
the code, because `soupyc_run` frees the parent context on the way out and the
child is meant to outlive the `soup` command. `clone_code` relocates node links
by a fixed delta (one contiguous pool) and re-interns literals into the child's
store. `sleep(ms)` exists too, bounded at 10 s.

## soupyc is reentrant (v0.10.1)

State lives in `soupyc_ctx_t`, one per running script, allocated in
`soupyc_run` and hung off `task_current()->soupyc`. **The state names are
accessor macros** (`pool`, `tok`, `src`, ...), each resolving through the
current task, which is why the interpreter body reads as plain names.

Consequences when editing `soupyc.c`:

- A new piece of interpreter state goes in the context plus a macro beside the
  others, never a new file-scope static. **Members carry an `m_` prefix** so
  that code holding a context pointer can say `ctx->m_x`; without it the bare
  name expands into the member position.
- **A macro name must not collide with any struct member name**, or it expands
  into the member position and the error points at the `#define`. That is why
  `sc_state`'s array member is called `arr_slots`.
- Inside `soupyc_run`, reach state through the macro, not `ctx->member`.
- `sc_state` stays global because the challenge's stage 4 depends on its
  layout; a compile-time assertion freezes the two offsets. Each context
  releases only the array slots it allocated, and there is deliberately no
  blanket clear at startup.

## soupyc strings are heap-backed (v0.10.0)

`val_t` is a pointer and a length. Strings live in a **per-run arena**
(`str_alloc`), released in full by `str_store_reset()` when a script ends, with
a 2 MB budget that turns a runaway loop into a clean error rather than starving
the heap Doom needs.

**Nothing is freed individually, on purpose.** `val_t` is copied by value
everywhere, so a string can be referenced from several places with no record of
how many, and the temporaries live in C locals where no collector can see them.
If you add a builtin, allocate with `str_alloc` and return `mksval_ref`; never
`kfree` a string. `SVAL_LEN` is now only the identifier length.

## Permissions are one rule (v0.48.0)

Processes too (v0.51.0): `proc_t.owner` is set at spawn; `may_signal()` in shell.c
gates kill/plate/steep; kernel tasks are the headchef's to kill.

`users_may(path, need)` in users.c is THE check, used by the shell's `may()` and
by the open/unlink/mkdir syscalls. A new syscall that touches a file asks it
too, the way the matching shell command does. The open check is
`#ifdef NO_CHALLENGE`: the challenge build's unchecked open is deliberate. Copy
any user path with `copy_user_path`, never read it in place. In tests, check
the key-typer's exit status: an untypeable character ends the sequence silently.

## Do not edit while check.sh runs (v0.52.1)

check.sh builds once at its start but reads each test script when that test
starts: editing scripts mid-run tests new scripts against the old binary.

## qemu_keys waits for the login banner (2026-10-08)

scripts/qemu_keys.py finds the QEMU process on the other end of the monitor
socket (SO_PEERCRED), reads its `-serial file:` argument, and waits for "clock
in to start your shift" in that log before typing. `info chardev` does NOT
show a file chardev's path. QEMU_KEYS_NOWAIT=1 opts out. Proof: freeze QEMU
for 6 s at 0.3 s into the boot; the old driver garbled the login 3 of 3.

## Kitchen names (v0.60.6)

Every new command gets a kitchen name: shell builtins AND ring-3 programs
(sift.elf, not grep.elf; spoon.elf, not cat.elf). Tests compare behaviour with the host tool under
its real name; only the soupOS side is kitchen-named. sh syntax keywords
(for, if, while, ...) stay as sh has them.

## Homes (v0.52.2)

Ordinary cooks start in /home/<cook> (`ensure_home` in shell.c); the headchef in /.
Tests that log in as a cook must use absolute paths for files, or expect home.
`cook` falls back to the root for programs.

## Boot (v0.47.0)

The kernel leases an address at boot (kernel.c); tests do not need `plumbing dhcp`.
`/etc/rc` lines run as headchef before the login prompt (`run_rc` in shell.c).

## Secrets (v0.46.0)

users.c has two kinds: SOUP32 (the CHALLENGE build's, challenge stage 2, never
change it there) and PBKDF2 (the ordinary build's, `name:uid:$p$iters$salt$key`).
`MAKE_PBKDF2` follows `NO_CHALLENGE`. An old entry upgrades at its first good
login. scripts/users-test.sh judges from /etc/kitchen via mtools. When a test
drives a command that prompts (hire asks for the secret twice), answer every
prompt, or the following commands are swallowed as input and silently skipped.

## FAT metadata, and keeping disk.img pristine (v0.41.0)

Owner and mode are in directory-entry bytes 13 (200 + uid) and 14 (mode), via
`meta_get`/`meta_put` in fat.c; byte 12 is FAT's case flags and must stay 0.
Never write soupOS data into a FAT field another tool interprets; check with
`fsck.fat -n` (scripts/fsck-test.sh). Every script boots a COPY of disk.img;
check.sh fails if disk.img changes during a run.

## `fat_read` reports the file's size, not the bytes it stored (v0.39.1)

`*out_size` can exceed `bufsize`. Clamp it before using it as a byte count,
or refuse the file. Five callers did not: hatch leaked kernel memory over the
network with it, and three wrote a NUL past their buffers. For anything that
may be large, stream with `vfs_open`/`vfs_read` instead, as hatch now does.

## The vault is per connection (v0.42.0)

Failed passwords pause the connection and count against the address
(`fail_table`, 10 in 5 minutes locks it; v0.50.0). Every SSH test connects from
10.0.2.2, so a test that fails passwords on purpose must boot its own soupOS.

`conn_slots[SSH_MAX_CONN]` (4), a worker task each; `S` is `*cur_ssh()`, the
slot of the calling task. Code that runs on a SESSION task (the terminal's
`stream_write`) must use its ctx pointer, never `S`. Any new buffer in ssh.c
goes in `ssh_t`, not in a static. TCP has 8 connection slots so a refusal
always has one to land on.

## SSH keys, exec, busy (v0.34.0)

`/AUTHKEYS` lines are `cook ssh-ed25519 <b64>` (a bare `.pub` line = headchef only,
v0.49.0); `key_authorized(user, ...)` in `ssh.c` matches the client's blob for that cook and `ed25519_verify` checks the request
signature. `exec` runs through the shared shell and ends when the prompt is
seen in the output (`watch_for_prompt`); change the prompt format and update
that matcher. A second client gets a plain DISCONNECT(12) from
`refuse_others()`, which runs at the packet-wait point. Rekeying (v0.38.0):
a KEXINIT in `connection_loop` goes to `rekey()`; `maybe_start_rekey()` sends
ours after an hour or `vault rekey N` packets; `session_id` is set once.

## Sessions: a terminal and a shell per task (v0.36.0)

`task_t.term` and `task_t.shell` are inherited by every spawned task (and
cleared by `task_set_service`). Print on a task's behalf with
`term_current()`, never `vga_*`, unless it is about the physical screen; read
with `term_current()->getc` and treat -1 as "hung up". `cur_shell()` is the
asking task's shell_t; users.c/fat.c resolve the current uid through it. A
process remembers its terminal in `proc_t.term` (NULL = console); job lookups
are per terminal (`proc_most_recent_on`, `proc_report_finished_on`), and
`term_t.fg` is the terminal's foreground program. `shell_session_start` is the
API the vault uses; anything new that runs a shell elsewhere should use it too.
Commands that draw on the physical screen start with `needs_console("name")`.

## The shell writes to a terminal (v0.35.2)

In shell.c, use `t_puts`/`t_printf`/`t_color`/`t_cursor`/`t_getc`/... (they go
to `cur_shell()->term`), never `vga_*`/`keyboard_*` directly, unless the code is
about the physical screen (cells, the serial mirror). `term_vga` in `term.c` is
the console. All printf-style formatting is `kvformat` in str.c; do not grow a
second formatter. Other modules (soupyc, selftest, ring-3 stdout) still print
via vga_* and must be routed before a shell runs on a non-console terminal.

## Per-shell state is `shell_t` (v0.35.1)

`cwd`, history, `prompt_row`/`prompt_len` and the pager rows live in
`shell_t`, reached by `cur_shell()` (one instance today). New per-shell state
goes in the struct, not in a file-scope static; the logged-in user and the
foreground job are still global and move when the SSH session gets its own
shell (queue item 2c).

## Swap lives past the FAT volume (v0.35.0)

`swap.c` addresses the sectors between `fat_volume_sectors()` and
`ata_total_sectors()` by page slot; the Makefile's 48 MB image leaves 16 MB
there (an old 32 MB disk.img has no swap: `rm disk.img && make disk`).
`usermode_demand_page` tries `swap_in` first, then evicts with
`swap_out_one` at `PROC_PAGE_CAP`. Eviction is approximate LRU (v0.43.0): accessed
bits sampled every 32 evictions into `pr->last_ref`, oldest stamp goes; `pr->fifo`
is now an unordered resident set. Before changing the policy, model it on the
host against fridge.elf - the model matched the kernel to the page; only
heap pages are ever swapped. `swap_release` runs at process exit. Since v0.39.0 the ATA driver uses bus-master DMA
(PIIX BAR4, bounce buffer `dma_buf`, PIO fallback): under KVM, PIO cost one VM
exit per 16-bit word, which was 85% of swap's time.

## The heap is demand-paged (v0.33.0)

Since v0.37.0 the user stack is too: `USTACK_MAX` (1 MB) with four pages
mapped at exec, the rest on demand, and `USTACK_GUARD` never mapped.

`SYS_SBRK` only moves `pr->brk`. `usermode_demand_page()` (called first thing
in `isr_handler` for vector 14, **before** the ring-3 kill and the ring-0
panic) maps a zeroed page for a not-present fault inside `[USER_HEAP, brk)`,
up to `PROC_PAGE_CAP`, and the instruction retries. Two consequences to keep
in mind: the kernel may fault while copying into user memory (that is handled,
and `proof.elf read` proves it), and a program is killed at first touch past
the cap, not at sbrk. The user stack and the ELF image are still mapped
eagerly at exec. `pr->demand_pages` is reported on the exit line.

## soupyc file I/O, and the disk image is not a build product of `make` (v0.32.0)

`lines(path)` and `write_lines(path, array)` live with the other file builtins
in `soupyc.c`; `scripts/lines-test.sh` compares the written bytes with the host.
The scripts and seed files on the disk are `printf | mcopy` recipes in the
Makefile's `$(DISK)` rule, and **`make clean` leaves disk.img alone**: after
changing an embedded file, `rm disk.img && make disk`, or the gate boots the old
image and fails on a "missing" file.

## Idle is counted, klog lines are whole, the FAT knows its free count (v0.31.0)

- Every wait loop halts through `cpu_halt()`, never a bare `hlt`: the timer
  charges a halted tick to idle (`task_idle_last()`), which is what makes
  `ps`/`kitchen` CPU% true. A new `hlt` anywhere else makes its caller look busy.
- **Interrupt handlers do not klog** (audited and soaked 2026-10-07: none do, and the
  queue-item-5 note says why that is what keeps serial lines whole). Log from the task
  the IRQ wakes instead.
- `klog()`/`klog_puts()` hold preempt_disable for the whole line. That is what
  fixed the "flaky" job-control check: it was the kernel's "continued" line
  torn by the resumed process's output. An IRQ handler's klog can still tear a
  task's line; nothing asserts on that today.
- `fat_space()` returns a count maintained in `fat_set_entry`; the first call
  after a mount scans. Anything that writes the FAT must go through
  `fat_set_entry` or the count drifts (the `sample` balance check would catch it).
- The hardware cursor is written once per string (`flush_cursor` in the public
  vga wrappers) and never on a RAM-backed cell store. `vga_set_mirror(0)` turns
  the serial mirror off for a frame drawn in place; always turn it back on.
- `scripts/jobcontrol-soak.sh` run six at a time is how to reproduce
  switch-timing bugs; the serial-load that parallel QEMUs put on the host
  surfaces them, the single-boot gate never did.

## The gate is segments (v0.30.2)

`scripts/gate/*.sh` each boot their own QEMU and run at once; `gate.sh` is
the runner. The split follows one rule: **a check that counts lines (two IRQ
kills, three `killed`, the last two greet.elf exits) lives in the segment
whose drive sequence produces every line it counts.** Add a drive step and its
checks to the same segment; a new subsystem gets a new segment, which is cheap
(it adds nothing to the wall time until it is the slowest). The serial logs
are `/tmp/soupos-gate-<segment>.*.log` under `KEEP=1`.

## Two things about the gate that cost me time (v0.10.0)

- **It deletes its serial log unless `KEEP=1`.** `ls -t /tmp/soupos-gate-*.log`
  will cheerfully hand you one from hours ago, and a stale log can show symptoms
  of problems that were fixed long since. Check the timestamp.
- **When adding drive commands, confirm you added rather than replaced.** A
  dropped drive line fails as "no match for ..." on a completely unrelated
  check, which reads exactly like a kernel regression in that subsystem.

## Drag-select in jot (v0.9.9)

The editor waits on the keyboard **or** the mouse (it used to block in
`keyboard_getchar`, which cannot see a mouse). Press sets `emark`, dragging
moves `ecur`, and since that is the pair the keyboard selection already uses,
^C/^X/^V need no new code. `cell_to_offset` clamps past a line's end to its end
and past EOF to the end of the buffer.

**If a selection test comes back empty, check the pointer's row before the
code.** A press past the last line puts mark and cursor at the same clamped
offset, which is an empty selection and looks exactly like a broken driver.

**The gate's TCP listeners take their accept timeout as a budget for the whole
drive sequence** (now 900s against a ~320s run). Adding checks ahead of the TCP
ones eats into it, and when it expires the listeners exit and the TCP checks
fail looking like a kernel regression.

## PS/2 mouse (`src/mouse.c`, v0.9.8)

The i8042's auxiliary device on IRQ 12. Positions are text cells so they line
up with the screen; four raw counts per cell. Three gotchas, all commented in
the file: IRQ 12 needs `irq_unmask` (slave line, so the cascade too), commands
go through a `0xD4` prefix and must wait for the `0xFA` acknowledgement, and
the three-byte packets have no framing - bit 3 of the first byte is always set,
and resynchronising on it is the only thing keeping a dropped byte from
misaligning the stream permanently. `skewer` shows the state.

## `sample`: in-kernel self-tests (v0.9.7)

`src/selftest.c`, run by the `sample` command: allocator patterns (including a
reverse free, which is the case a one-directional coalescer fails), the VFS
handle table at exhaustion, the pipe ring across its wrap checked by content,
a known-answer checksum, a FAT round trip and the hand-written string routines.
Self-contained, so it is safe on a live machine. It ends with
`[sample] N tests, M failed`, which the gate greps; add tests freely, the gate
pattern does not care about the count.

## What is reachable from two tasks (v0.9.6)

`docs/preemption-audit.md` answers this per file. The short version: the pipe
ring and the network transmit staging buffers were genuinely racy and are now
guarded; `soupyc`, `users.c` and `shell.c` are reached only from the shell task
and are safe **until `spawn()` exists**; `klog`'s ring is shared with interrupt
handlers but can only interleave lines, never escape the buffer, and is left
alone deliberately. Add to that document when adding state, and check
reachability by call graph rather than by looking for missing locks.

## Allocator locking (v0.8.5)

Since processes became concurrent, anything that allocates is reachable from
several tasks at once. `pmm_alloc_page`/`pmm_free_page` take `irq_save` around
the bitmap claim (a timer preemption between `is_used()` and `set_used()` hands
one frame to two processes), `heap.c` already did the same for its free list,
and `vfs.c`'s `alloc_node` uses `preempt_disable` for the handle table. None of
these critical sections yield, which is why interrupts-off is sufficient and a
mutex would be overkill. `user/memtest.c` plus three pipeline stages is the
regression test.

## Clipboard (`src/clip.c`, v0.8.4)

One 4 KB fixed buffer shared by everything that edits text. There is no mouse
driver, so selection is keyboard-driven.

- **jot**: Ctrl+B anchors a mark (either side of the cursor; `sel_span()`
  orders them), the span renders in inverse video, Ctrl+C copies, Ctrl+X cuts,
  Ctrl+V pastes. `[MARK]` shows in the status bar while a mark is set.
- **Shell line editor**: Ctrl+U and Ctrl+W copy what they kill, so Ctrl+V puts
  it back. A pasted newline or tab becomes a space, since the prompt is one
  line.
- Both talk to `clip.c` rather than to each other, which is why a cut in the
  editor pastes at the prompt.
- `scraps` prints the contents and mirrors them to the kernel log, which is how
  the headless gate checks a copy by its text.

## Processes (`src/proc.c`, v0.8.0)

A process **is** a kernel task plus a `proc_t`, and the pid **is** the task id,
so `ps` shows one kind of thing and the scheduler, wait queues, mutexes and
reaper are reused unchanged.

- `proc_spawn(path, args, background, &pid)` claims one of `PROC_MAX` (8)
  static slots and spawns a task on `proc_entry`, which calls `usermode_run`
  and then `proc_exit`. Slot claim and task linking happen under
  `preempt_disable` so two shells cannot take one slot.
- **The slot outlives the task deliberately.** `task.c`'s reaper frees a DEAD
  `task_t` and its 16 KB stack, so an exit code kept on the task would vanish
  before the parent read it. A finished process stays `PROC_ZOMBIE` until
  `proc_wait` collects it (foreground) or `proc_report_finished` announces it
  at the next prompt (background).
- **Foreground** means the shell calls `proc_set_foreground` then blocks in
  `proc_wait`, which re-checks the child's state inside a `preempt_disable`
  region before blocking (the same lost-wakeup guard `mutex_lock` uses).
- **Killing** sets a flag only (`proc_kill` / `proc_flag_kill`). Marking the
  task DEAD would leak the whole address space, because the loader would never
  return to free it. The flag is acted on where the kernel holds no locks:
  - top and bottom of `syscall_dispatch`;
  - an IRQ that interrupted **ring-3 code**, tested as `(regs->cs & 3) == 3`
    in `irq_handler`. This is the only way to kill a program that makes no
    syscalls at all.
  A timer IRQ that landed in the kernel half of a syscall must **not** unwind:
  that code may hold the FAT mutex, and abandoning its frame would leave the
  filesystem locked forever. Teardown then runs through the same unwind as a
  fault, so fds, address space and task come down normally.
- **Ctrl-C** is raised in the keyboard IRQ, not the shell: while a foreground
  program runs the shell is blocked in `proc_wait` and reads no keys, so `0x03`
  flags the foreground process and is swallowed. With no foreground process it
  reaches the shell and cancels the input line as before.
- **Stopping** (v0.8.2): `PROC_STOPPED` is a live process parked on
  `cont_wq` at one of the same three safe points. `proc_take_stop` enables
  interrupts to park (the safe points are interrupt gates and `task_block_on`
  ends in `hlt`) and restores the caller's IF after. Ctrl-Z (`0x1A`) raises it
  from the keyboard IRQ, like Ctrl-C. `proc_kill` continues a stopped process
  as well as flagging it, otherwise the kill would never be noticed.
- `proc_wait(pid, &code)` returns 1 for exited (slot collected), 0 for stopped
  (slot kept), -1 for no such process.
- **Pipes and redirection (v0.8.3)**: `vfs_pipe()` returns two ordinary
  `vfs_node_t` handles over one 4 KB ring. A reader blocks while empty with a
  writer alive and reads 0 (EOF) once the last writer closes; a writer blocks
  while full with a reader alive and fails once every reader closes. Both
  abandon the wait when the process has a kill pending, which is what keeps a
  program parked on a dead pipe killable.
- **fds 0-2 are bindable**: `proc_t.ufds[fd]` wins over the console default, so
  `proc_spawn` can hand a process its stdin/stdout. `usermode_run` clears the
  table from fd 3 up, deliberately: clearing from 0 would undo redirection.
  The process owns what it was given and closes it on exit, which is what
  sends EOF down a pipe.
- Shell surface: `cook a.elf | b.elf | c.elf` (4 stages max), `< in`, `> out`,
  `cook prog &`, `orders`, `plate [pid]`, `steep [pid]`, `kill <pid>`, `kill %` (newest
  job), `ps` marks which tasks are processes, and a finished background job is
  announced before the next prompt.

## Doom Port — performance notes (v0.8.1)

Measure before changing anything here: `make PROFILE=1` builds the port with
per-phase `rdtsc` counters that klog fps and kilocycles per phase (flats, bsp,
thinkers, sprites, status bar, vblank, VRAM copy) every 50 frames, readable
from the serial log headless.

- **`draw_flats` is the hot path**, and was 89% of the frame when it called
  `find_sector_at()` per pixel (a BSP descent plus four dependent lookups,
  53,760 times a frame). `flat_row_runs()` now samples the row every 16 pixels
  and bisects where samples disagree, because the floor along a screen row is a
  straight line in world space and changes sector only at boundaries. Still the
  largest single item, now split between descents and actual texturing.
- **Run it with KVM.** `make run` probes `/dev/kvm`; TCG costs ~6x. The smoke
  test stays on TCG with the stock qemu32 CPU on purpose (no SSE2, which is
  what catches auto-vectorisation regressions).
- **Innocent until measured:** the vblank spin on port 0x3DA is ~15k cycles
  (QEMU's default retrace emulation toggles the bit rather than timing it) and
  the whole 64,000-byte VRAM copy is ~33k. Neither is worth optimising.
- **Angles are `ANG_N` (128) steps**, not a bare 32. Use `ANG_MASK`, `ANG_90`,
  `ANG_270`, `ANG_PER_DIR` (8-way sprite rotations) and `ANG_SKY_U`.

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

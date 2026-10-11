# Mincecraft Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A first-person voxel sandbox with light survival in a food world, `cook mincecraft.elf` on the soupOS desktop.

**Architecture:** A ring-3 program in `user/mincecraft/` (its own Makefile rule) casts one ray per pixel through a 128x128x64 byte grid in 16.16 fixed point, renders at 320x240, doubles it into a 640x480 window and hands it over with `SYS_WIN_PUT`. Two small kernel-side changes come first: `SYS_WIN_KEYS` (held keys for the focused, unlocked window) and `HOME` in every program's environment.

**Tech Stack:** freestanding C (gnu99, `-m32 -mgeneral-regs-only`, no libc, no FPU), the soupOS `ulib`, NASM-free; tests in bash plus Python 3 driving headless QEMU through `scripts/qemu_keys.py`.

**Spec:** `docs/superpowers/specs/2026-10-11-mincecraft-design.md` (commit 5b9bb34). Read it before starting; this plan argues from it.

## Global Constraints

- Work in `~/Projects/soupOS-processes`, branch `pipes-wip`. **Before Task 1**, the uncommitted compost work (v0.60.189: `CLAUDE.md ROADMAP.txt docs/NIGHT-QUEUE.md scripts/check.sh scripts/ctfilesmenu-test.sh src/countertop.c scripts/ctcompost-test.sh`) must be committed by the user's say-so or left alone; every commit in this plan uses **explicit paths** so it never sweeps those files in.
- **No floating point anywhere.** Never remove `-mgeneral-regs-only`. All maths is 16.16 (`fx`), products through `long long`.
- **Kitchen names.** The program is `mincecraft.elf`; `mince.elf` and `scripts/mince-test.sh` already exist and are not touched. Before writing any `scripts/X-test.sh`, run `ls scripts/X-test.sh` to confirm the name is free.
- **No em-dashes** in code comments, docs or commit messages.
- **One item, one commit.** Each task bumps the version: Task 1 v0.60.190, Task 2 v0.60.191, Task 3 v0.60.192, Task 4 v0.60.193, Task 5 v0.60.194, Task 6 v0.60.195, Task 7 v0.60.196, Task 8 v0.60.197. Each commit adds a ROADMAP.txt changelog entry in the house style (version, date, title in capitals, indented bullets) and moves "Current version:" and CLAUDE.md's version line.
- **The pre-commit gate is `./scripts/remote-check.sh`** (CT 123 on atlas; `ssh atlas pct start 123` if it is down). While working, run single test scripts locally. Every new test script is added to the list in `scripts/check.sh` (the `tests=` block around line 120).
- After any change under `user/`: `make user`, then refresh the disk: `rm disk.img && make disk` (`make disk` is a no-op when `disk.img` exists).
- Window body at most 640x480, `0x00RRGGBB`. Resident pages under 4 MB (`PROC_PAGE_CAP`).
- Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Esc and the desktop.** Esc must reach the game as `WEV_KEY` 27 and quit with a save; check that countertop does not take Esc for itself while a program window has the focus (Task 5 test presses Esc and asserts the `bye` and `saved` markers).
2. **Focus lost mid-walk.** Holding W, then clicking another window, must stop the player at once (the bitmap goes to zeros). Task 1 pins the syscall side; Task 5 asserts the player marker stops changing once the focus has moved.
3. **A world file from somewhere else.** Truncated, oversized, a wrong version, a byte flipped, trailing bytes: each refused with a bar note and a fresh world, never a crash or a half-loaded grid. Task 8 tests the flipped byte, a truncated copy and trailing junk.
4. **Saving twice over a longer file.** `O_WRITE` must leave no tail from an earlier, longer save; the loader's end-of-file check catches it. Task 8 saves a busy world, then a simpler one, and loads.
5. **Breaking the block you stand on, and placing into yourself.** Breaking underfoot drops the player one block; placing into the player's own box is refused. Task 6 asserts both.

---

## File structure

| Path | Responsibility |
|---|---|
| `src/keyboard.c`, `src/keyboard.h` | `keyboard_key_bitmap(uint8_t out[32])`: `key_state` as bits |
| `src/countertop.c`, `src/countertop.h` | `countertop_win_keys(pid, id, buf)`: ownership, focus and lock checks |
| `src/syscall_nr.h` | `SYS_WIN_KEYS 24` |
| `src/usermode.c` | the `SYS_WIN_KEYS` case |
| `user/ulib.c`, `user/ulib.h` | `sys_win_keys`, `SC_*` scancodes, `key_held` |
| `user/prod.c` | a `keys` mode for the syscall's test |
| `src/shell.c` | `build_env` hands `HOME` |
| `user/mincecraft/fix.h`, `fix.c`, `sintab.h`, `gen_sintab.py` | fixed point and sine |
| `user/mincecraft/blocks.h`, `blocks.c` | the block table and textures |
| `user/mincecraft/world.h`, `world.c` | grid, generation, flat world, hash |
| `user/mincecraft/ray.h`, `ray.c` | the DDA ray cast, shared by render and player |
| `user/mincecraft/render.h`, `render.c` | the view into the 640x480 body |
| `user/mincecraft/player.h`, `player.c` | physics, breaking, placing, hotbar |
| `user/mincecraft/hud.h`, `hud.c` | crosshair, hotbar, names, clock |
| `user/mincecraft/save.h`, `save.c` | the world file |
| `user/mincecraft/main.c` | arguments, window, loop, markers |
| `scripts/winkeys-test.sh`, `scripts/mincecraft-gen-test.sh`, `scripts/mincecraft-view-test.sh`, `scripts/mincecraft-play-test.sh`, `scripts/mincecraft-save-test.sh` | tests |
| `scripts/mincecraft_gen.py` | the generator again, in Python, for the gen test |

---

### Task 1: `SYS_WIN_KEYS`, held keys for the focused window (v0.60.190)

**Files:**
- Modify: `src/keyboard.c` (after `keyboard_key_pressed`), `src/keyboard.h:57`
- Modify: `src/syscall_nr.h` (after `SYS_NOTE`, line 37-39)
- Modify: `src/countertop.c` (new function after the cloche's `static int locked` at line 888), `src/countertop.h:25`
- Modify: `src/usermode.c` (new case after `SYS_WIN_EVENT`, about line 722)
- Modify: `user/ulib.c`, `user/ulib.h`
- Modify: `user/prod.c` (a `keys` mode)
- Create: `scripts/winkeys-test.sh`
- Modify: `scripts/check.sh` (list), `ROADMAP.txt`, `CLAUDE.md`

**Interfaces:**
- Produces: `int sys_win_keys(int id, unsigned char out[32])` returning 0 or -1; `static inline int key_held(const unsigned char *k, int sc)`; scancodes `SC_W 0x11, SC_A 0x1E, SC_S 0x1F, SC_D 0x20, SC_F 0x21, SC_SPACE 0x39, SC_UP 0xC8, SC_DOWN 0xD0, SC_LEFT 0xCB, SC_RIGHT 0xCD`.

- [ ] **Step 1: Write the failing test.** First `ls scripts/winkeys-test.sh` (must not exist). The test boots, starts `prod.elf victim &` at the console (it opens window id 0 once the desktop is up, as in `prodwin-test.sh`), writes `/countertop` with `idle 3` so the cloche comes down after three idle seconds (as `ctcloche-test.sh` does), starts the desktop, opens a terminal and runs `cook prod.elf keys`, which opens window id 1. Phases, each separated by `WAIT:2`:
  1. prod's window focused: `KEY:w 1000` (a raw `sendkey w 1000`, held a second) then `WAIT:2`.
  2. Click the terminal window (focus moves): `KEY:w 1000`, `WAIT:2`.
  3. Click prod's body (focus back), `WAIT:4` (the cloche comes down), `KEY:w 1000`, `WAIT:2`, type `rosemary` (unlock), `WAIT:2`.
  4. `KEY:w 1000`, `WAIT:2`, then `TYPE:q` (prod's keys mode ends on q).

  Window positions: copy `prodwin-test.sh`'s for the victim and the terminal. For prod keys' window, take one `KEEP=1` run with a `MON:screendump` after it opens and read its body origin off the picture, then hard-code it with a comment, as the other ct tests do.

  Assertions on the serial log:
  - exactly two `[prod] keys 11` lines (phases 1 and 4), each followed by `[prod] keys none`;
  - no `[prod] keys` line naming anything other than `11` or `none`, which proves that phase 3's typed password never showed;
  - `[prod] keys bad-id -1`, `[prod] keys not-mine -1`, `[prod] keys bad-buf -1`, printed by the mode at start.

```bash
#!/usr/bin/env bash
# winkeys-test.sh - SYS_WIN_KEYS (v0.60.190): the keys held, as a bitmap,
# only for the caller's own window while it has the focus and the desktop is
# not locked. prod.elf keys prints the held scancodes whenever they change.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-winkeys.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'idle 3\n' | mcopy -i "$WORK/disk.img" -o - ::countertop
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1")
# Positions: TERM_* is the terminal's title bar, KEYS_* prod keys' body,
# measured from a KEEP=1 screendump (see the plan, Task 1 step 1).
TERM_CLICK=("MON:mouse_move <dx> <dy>")   # replace with the measured moves
KEYS_CLICK=("MON:mouse_move <dx> <dy>")
keys=("headchef" "rosemary" "WAIT:1" "cook prod.elf victim &" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" "cook prod.elf keys" "WAIT:3"
      "${KEYS_CLICK[@]}" "${CLICK[@]}" "KEY:w 1000" "WAIT:2"
      "${TERM_CLICK[@]}" "${CLICK[@]}" "KEY:w 1000" "WAIT:2"
      "${KEYS_CLICK[@]}" "${CLICK[@]}" "WAIT:4" "KEY:w 1000" "WAIT:2" "rosemary" "WAIT:2"
      "KEY:w 1000" "WAIT:2" "TYPE:q" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK/serial.log" <<'PY' || fail=1
import re, sys
log = open(sys.argv[1], 'rb').read().decode('latin-1')
lines = re.findall(r'\[prod\] keys (.*)', log)
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= cond
for want in ('bad-id -1', 'not-mine -1', 'bad-buf -1'):
    check(want in lines, 'limits: ' + want)
held = [l for l in lines if not l.startswith(('bad-', 'not-'))]
check(held == ['11', 'none', '11', 'none'], 'held keys seen only while focused and unlocked: %r' % held)
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "winkeys-test: PASS" || echo "winkeys-test: FAIL"
exit "$fail"
```

  Replace the two `<dx> <dy>` placeholders with measured numbers before the first commit; a script with them left in must not be committed.

- [ ] **Step 2: Add prod's `keys` mode** so the test has something to run (it fails until the syscall exists, since `sys_win_keys` returns -1 for everything):

```c
/* (v0.60.190) keys: SYS_WIN_KEYS. Its limits first, then, until q, the held
 * scancodes in hex whenever they change ("none" for none). */
static void keys_mode(void) {
    unsigned char k[32], last[32];
    int id = -1;
    for (int i = 0; i < 600 && id < 0; i++) { id = sys_win_open(160, 80, "keys"); if (id < 0) sys_sleep(100); }
    if (id < 0) { eprint("[prod] keys no window\n"); return; }
    eprint("[prod] keys bad-id ");   eprint_int(sys_win_keys(99, k));   eprint("\n");
    eprint("[prod] keys not-mine "); eprint_int(sys_win_keys(0, k));    eprint("\n");   /* the victim's */
    eprint("[prod] keys bad-buf ");  eprint_int(sys_win_keys(id, (unsigned char *)ABOVE_BRK)); eprint("\n");
    for (int i = 0; i < 32; i++) last[i] = 0;
    for (;;) {
        uwinev_t e;
        int g = sys_win_event(id, &e, 50);
        if (g < 0 || (g == 1 && (e.type == WEV_CLOSE || (e.type == WEV_KEY && e.key == 'q')))) break;
        if (sys_win_keys(id, k) < 0) continue;
        int same = 1;
        for (int i = 0; i < 32; i++) if (k[i] != last[i]) same = 0;
        if (same) continue;
        for (int i = 0; i < 32; i++) last[i] = k[i];
        eprint("[prod] keys");
        int any = 0;
        for (int sc = 0; sc < 256; sc++) if (key_held(k, sc)) {
            static const char hx[] = "0123456789abcdef";
            char b[4] = { ' ', hx[sc >> 4], hx[sc & 15], 0 };
            eprint(b); any = 1;
        }
        eprint(any ? "\n" : " none\n");
    }
}
```

  Wire it into prod's mode dispatch next to `victim` and `limits` (`if (!strcmp_(m, "keys")) { keys_mode(); return 0; }`, using whatever string compare prod already uses for its modes), and add a line to the usage comment at the top: `cook prod.elf keys  (v0.60.190) SYS_WIN_KEYS: its limits, then the held keys`.

- [ ] **Step 3: Run the test to see it fail.** `make user && rm disk.img && make disk && make && ./scripts/winkeys-test.sh`. Expected: FAIL, with the held list empty. Until the syscall exists, `sys_win_keys` falls through to the default case and returns -1.

- [ ] **Step 4: Implement.** In `src/keyboard.c`:

```c
/* The keys held now as bits, scancode n at out[n >> 3] bit (n & 7);
 * extended keys at 0x80 | code (v0.60.190, for SYS_WIN_KEYS). */
void keyboard_key_bitmap(uint8_t out[32]) {
    for (int i = 0; i < 32; i++) out[i] = 0;
    for (int n = 0; n < KEY_STATE_SIZE; n++) if (key_state[n]) out[n >> 3] |= (uint8_t)(1u << (n & 7));
}
```

  `src/keyboard.h`: `void keyboard_key_bitmap(uint8_t out[32]);   /* key_state as bits (v0.60.190) */`.

  `src/syscall_nr.h`, after `SYS_NOTE`:

```c
#define SYS_WIN_KEYS 24 /* ebx = id, ecx = 32 bytes: the keys held, scancode n at bit n
                         * (extended keys at 0x80|code), if the window is the caller's,
                         * placed, the focus and the desktop not locked; else all zeros.
                         * -> 0, or -1 (not its window, a bad buffer) (v0.60.190)      */
```

  `src/countertop.c`, after line 888 (`static int locked, ...`) so `locked` is in scope:

```c
/* SYS_WIN_KEYS (v0.60.190): the held keys for a program's window, only
 * while it is placed, the focus and the cloche is up. The lock check
 * matters: key_state sees what is typed at the cloche too, and the
 * secret must never reach a program. */
int countertop_win_keys(uint32_t pid, int id, uint8_t *out) {
    mutex_lock(&app_mtx);
    app_t *a = app_mine(pid, id);
    int ok = a != 0, live = a && a->win >= 0 && a->win == topmost() && !locked;
    mutex_unlock(&app_mtx);
    if (!ok) return -1;
    if (live) keyboard_key_bitmap(out);
    else for (int i = 0; i < 32; i++) out[i] = 0;
    return 0;
}
```

  `src/countertop.h`: `int countertop_win_keys(uint32_t pid, int id, uint8_t *out);   /* 32 bytes: 0, or -1 */`. Make sure `countertop.c` includes `keyboard.h` (it reads keys already; add the include if the compiler says so).

  `src/usermode.c`, after the `SYS_WIN_EVENT` case:

```c
        case SYS_WIN_KEYS: {              /* (v0.60.190) the held keys, for mincecraft.elf */
            uint8_t k[32];
            if (!pr || !user_ok(r->ecx, 32)) { r->eax = (uint32_t)-1; break; }
            int got = countertop_win_keys(pr->pid, (int)r->ebx, k);
            if (got == 0) memcpy((void *)r->ecx, k, 32);
            r->eax = (uint32_t)got;
            break;
        }
```

  `user/ulib.c`, after `sys_note`:

```c
int sys_win_keys(int id, unsigned char *out) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(SYS_WIN_KEYS), "b"(id), "c"(out) : "memory");
    return ret;
}
```

  `user/ulib.h`, with the other window calls:

```c
int  sys_win_keys(int id, unsigned char *out);   /* 32 bytes of held keys; 0, or -1 (v0.60.190) */
/* Scancodes for sys_win_keys (set 1; extended keys 0x80 | code). */
#define SC_W 0x11
#define SC_A 0x1E
#define SC_S 0x1F
#define SC_D 0x20
#define SC_F 0x21
#define SC_SPACE 0x39
#define SC_UP 0xC8
#define SC_DOWN 0xD0
#define SC_LEFT 0xCB
#define SC_RIGHT 0xCD
static inline int key_held(const unsigned char *k, int sc) { return (k[sc >> 3] >> (sc & 7)) & 1; }
```

- [ ] **Step 5: Run the test to see it pass.** `make user && rm disk.img && make disk && make && ./scripts/winkeys-test.sh`. Expected: `winkeys-test: PASS`. Then `./scripts/prodwin-test.sh` (prod changed) and `./scripts/ctcloche-test.sh` (the lock is shared) must still pass.

- [ ] **Step 6: Changelog, list, gate, commit.** Add `winkeys-test` to `scripts/check.sh`'s list. Add the v0.60.190 entry ("HELD KEYS FOR A PROGRAM'S WINDOW") to ROADMAP.txt, and add `SYS_WIN_KEYS` to CLAUDE.md's syscall table if it has one (search for `SYS_NOTE`). Run `./scripts/remote-check.sh` and expect it to pass.

```bash
git add src/keyboard.c src/keyboard.h src/syscall_nr.h src/countertop.c src/countertop.h src/usermode.c \
        user/ulib.c user/ulib.h user/prod.c scripts/winkeys-test.sh scripts/check.sh ROADMAP.txt CLAUDE.md
git commit -m "Held keys for a program's window (v0.60.190)" -m "SYS_WIN_KEYS: the held keys as a bitmap, only for the caller's placed, focused window with the cloche up.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

  If the compost files are still uncommitted, `git add` of `src/countertop.c`, `scripts/check.sh`, `ROADMAP.txt` and `CLAUDE.md` would sweep their compost hunks in too. **Stop and ask the user** to commit or set aside the compost work first; do not use `git add -p` to pick hunks without saying so.

---

### Task 2: `HOME` in every program's environment (v0.60.191)

**Files:**
- Modify: `src/shell.c:6001-6015` (`build_env`)
- Modify: `scripts/env-test.sh`
- Modify: `ROADMAP.txt`, `CLAUDE.md`

**Interfaces:**
- Produces: `getenv("HOME")` in every `cook`ed program: `/` for the headchef, `/home/<cook>` for others, unless the cook handed their own HOME.

- [ ] **Step 1: Write the failing test.** `env-test.sh` lists the whole environment through `labels.elf | rack.elf` and compares it with bash's `env | grep -E '^S[A-E]=' | sort`. HOME would break E1 to E5, so filter the soupOS side too and add a HOME check. In `LINES`, change every `cook labels.elf | rack.elf` to `cook labels.elf | sift.elf S | rack.elf`. sift keeps lines containing "S", and every test variable starts with S. Make the host side's substitution match: `b=${l//cook labels.elf | sift.elf S | rack.elf/env | grep -E \'^S[A-E]=\' | sort}`. Then add two lines and their files:

```bash
  'cook labels.elf HOME > /E11.TXT'
  'hand HOME=/elsewhere ; cook labels.elf HOME > /E12.TXT ; discard HOME'
```

  `FILES="E1 ... E10 E11 E12"`. E11 compares with `/` and E12 with `/elsewhere`. Add a small block after the bash comparison loop that `mcopy`s E11 and E12 out and checks them directly rather than through bash. Bash's HOME is the host's, so these two lines must be left out of the bash loop: guard them with `case $f in E11|E12) continue;; esac` in that loop.

  The `sift.elf S` filter could let a HOME line through only if it contained a capital S, and `HOME=/` does not. Note that in a comment.

- [ ] **Step 2: Run to see it fail.** `make && ./scripts/env-test.sh`. Expected: E1 to E10 still pass (filtered) and E11 fails (empty).

- [ ] **Step 3: Implement in `build_env`.** Before the loop, if the shell has no handed variable named HOME, write `HOME=<home>\n`, where home is `/` for uid 0 and `/home/<name>` otherwise. That is the same path `ensure_home_inner` builds (shell.c:352), so factor it into `static void home_of(uint8_t uid, char *out)` and use it in both places:

```c
/* A cook's home bowl as a path: / for the headchef, /home/<cook> (v0.60.191,
 * shared by ensure_home and build_env). */
static void home_of(uint8_t uid, char *out) {
    if (uid == 0) { strcpy(out, "/"); return; }
    strcpy(out, "/home/");
    strncpy(out + 6, users_name_of(uid), FAT_PATH_MAX - 7); out[FAT_PATH_MAX - 1] = '\0';
}
```

  In `build_env`, before the loop (the shell's current uid is whatever `ensure_home`'s caller passes; find it with `grep -n "ensure_home(" src/shell.c`, which is likely `me->uid` or `users_current()`):

```c
    int own = 0;
    for (int i = 0; i < me->nvars; i++) if (me->vhand[i] && !strcmp(me->vname[i], "HOME")) own = 1;
    if (!own) {
        char home[FAT_PATH_MAX];
        home_of(CURRENT_UID, home);          /* the same uid expression ensure_home is called with */
        if ((int)strlen(home) + 6 < max) { strcpy(out, "HOME="); strcat(out, home); strcat(out, "\n"); n = (int)strlen(out); }
    }
```

  Replace `CURRENT_UID` with that expression. If `strcat` is not in `src/str.c`, build the line with `strcpy` at offsets, the way the loop below already does.

- [ ] **Step 4: Run to see it pass.** `make && rm disk.img && make disk && ./scripts/env-test.sh`. Expected: PASS for E1 to E12. Also run `./scripts/check.sh quick` locally to catch any gate segment that printed the whole environment.

- [ ] **Step 5: Commit** after `./scripts/remote-check.sh` passes, with the v0.60.191 entry ("HOME FOR EVERY PROGRAM"):

```bash
git add src/shell.c scripts/env-test.sh ROADMAP.txt CLAUDE.md
git commit -m "HOME for every program (v0.60.191)" -m "build_env hands HOME (the cook's home bowl) unless the cook handed one.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: build rule, fixed point, blocks, world generation (v0.60.192)

**Files:**
- Create: `user/mincecraft/fix.h`, `fix.c`, `gen_sintab.py`, `sintab.h` (generated, committed)
- Create: `user/mincecraft/blocks.h`, `blocks.c`, `world.h`, `world.c`, `main.c` (only `-gen` for now)
- Create: `scripts/mincecraft_gen.py`, `scripts/mincecraft-gen-test.sh`
- Modify: `Makefile` (USER_ELFS, a rule, the disk's mcopy), `scripts/check.sh`, `ROADMAP.txt`, `CLAUDE.md`

**Interfaces:**
- Produces: `fx`, `FX_ONE`, `fx_mul`, `fx_div`, `fx_sin(int a)`, `fx_cos(int a)` (a in 1024ths of a turn); `blocks[]`, `B_*` ids, `block_texel(id, face, tx, ty)`; `world`, `WX WZ WY WCELLS SEA`, `world_get`, `world_set`, `world_alloc`, `world_gen(seed)`, `world_flat()`, `world_hash()`, `world_top(x, z)`, `world_height(x, z, seed)`.

- [ ] **Step 1: Write the Python reference generator.** `ls scripts/mincecraft-gen-test.sh scripts/mincecraft_gen.py` must show neither. `scripts/mincecraft_gen.py` is world.c, integer for integer:

```python
#!/usr/bin/env python3
"""mincecraft_gen.py - world.c's generator, integer for integer (v0.60.192).
   python3 scripts/mincecraft_gen.py SEED   -> prints the FNV-1a hash and spawn top"""
import sys
WX, WZ, WY, SEA = 128, 128, 64, 26
AIR, CRUST, CRUMB, PARM, IRON, BROTH, SUGAR, STALK, FLORET, PEPPER, BUTTER = range(11)
M = 0xFFFFFFFF
def hash2(x, z, seed):
    h = (seed ^ ((x * 0x27d4eb2d) & M) ^ ((z * 0x165667b1) & M)) & M
    h ^= h >> 15; h = (h * 0x2c1b3c6d) & M
    h ^= h >> 12; h = (h * 0x297a2d39) & M
    h ^= h >> 15
    return h
def noise(x, z, s, seed):
    x0, z0, fx, fz = x // s, z // s, x % s, z % s
    v00 = hash2(x0, z0, seed) & 255; v10 = hash2(x0 + 1, z0, seed) & 255
    v01 = hash2(x0, z0 + 1, seed) & 255; v11 = hash2(x0 + 1, z0 + 1, seed) & 255
    a = v00 * (s - fx) + v10 * fx; b = v01 * (s - fx) + v11 * fx
    return (a * (s - fz) + b * fz) // (s * s)
def height(x, z, seed):
    return 20 + (noise(x, z, 32, seed) * 16 + noise(x, z, 8, seed ^ 0x9e3779b9) * 8) // 255
def gen(seed):
    w = bytearray(WX * WZ * WY)
    for z in range(WZ):
        for x in range(WX):
            edge = x == 0 or z == 0 or x == WX - 1 or z == WZ - 1
            h = height(x, z, seed)
            top = SUGAR if h < SEA + 2 else CRUST
            for y in range(WY):
                if edge or y == 0: b = IRON
                elif y < h - 4:
                    r = hash2(x + z * WX, y, seed ^ 0x51ed27)
                    b = PEPPER if r % 97 == 0 else (BUTTER if y < 12 and r % 211 == 1 else PARM)
                elif y < h - 1: b = SUGAR if top == SUGAR else CRUMB
                elif y == h - 1: b = top
                elif y < SEA: b = BROTH
                else: b = AIR
                w[(y * WZ + z) * WX + x] = b
    def get(x, y, z):
        if x < 0 or x >= WX or z < 0 or z >= WZ or y < 0: return IRON
        if y >= WY: return AIR
        return w[(y * WZ + z) * WX + x]
    for z in range(3, WZ - 3):
        for x in range(3, WX - 3):
            h = height(x, z, seed)
            if h < SEA + 2: continue
            r = hash2(x, z, seed ^ 0x7f4a7c15)
            if r % 60: continue
            t = 4 + (r >> 8) % 3
            for y in range(h, h + t): w[(y * WZ + z) * WX + x] = STALK
            cy = h + t
            for dy in range(-2, 3):
                for dz in range(-2, 3):
                    for dx in range(-2, 3):
                        if dx * dx + dy * dy + dz * dz <= 5 and get(x + dx, cy + dy, z + dz) == AIR:
                            w[((cy + dy) * WZ + z + dz) * WX + x + dx] = FLORET
    return w
def fnv(w):
    h = 2166136261
    for b in w: h = ((h ^ b) * 16777619) & M
    return h
if __name__ == '__main__':
    seed = int(sys.argv[1]) & M
    w = gen(seed)
    top = max(y for y in range(WY) if w[(y * WZ + 64) * WX + 64] not in (AIR, BROTH))
    print('seed=%d hash=%d top=%d' % (seed, fnv(w), top))
```

- [ ] **Step 2: Write the failing gen test.** It runs `cook mincecraft.elf -gen -seed N` at the console (no desktop needed) for seeds 1, 42 and 3000000000, and compares the program's `[mincecraft] world seed=N hash=H top=T` marker with the Python output:

```bash
#!/usr/bin/env bash
# mincecraft-gen-test.sh - Mincecraft's world generator (v0.60.192) against
# scripts/mincecraft_gen.py, the same integer maths in Python: the FNV-1a
# hash of the whole 128x128x64 grid and the spawn column's top, three seeds.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcgen.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
SEEDS="1 42 3000000000"
keys=("headchef" "rosemary" "WAIT:1")
for s in $SEEDS; do keys+=("cook mincecraft.elf -gen -seed $s" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
for s in $SEEDS; do
    want=$(python3 scripts/mincecraft_gen.py "$s")
    got=$(command grep -a -o "\[mincecraft\] world seed=$s hash=[0-9]* top=[0-9]*" "$WORK/serial.log" | head -1 | sed 's/\[mincecraft\] world //')
    if [ "$got" = "$want" ]; then echo "  ok    seed $s: $got"; else echo "  FAIL  seed $s: got '$got', want '$want'"; fail=1; fi
done
[ "$fail" = 0 ] && echo "mincecraft-gen-test: PASS" || echo "mincecraft-gen-test: FAIL"
exit "$fail"
```

  `qemu_keys.py` finds `serial.log` beside the monitor socket by itself (`serial_log()`), which `UNTIL:` needs, as in `env-test.sh`.

- [ ] **Step 3: Run it to see it fail.** `./scripts/mincecraft-gen-test.sh`. Expected: FAIL, because there is no `mincecraft.elf`.

- [ ] **Step 4: Fixed point.** `user/mincecraft/gen_sintab.py`:

```python
#!/usr/bin/env python3
# gen_sintab.py - writes sintab.h: sin(i*pi/512)*65536 rounded, i = 0..256
# (a quarter turn in 1024ths). Run once; the output is committed.
import math
v = [round(math.sin(i * math.pi / 512) * 65536) for i in range(257)]
print("/* sintab.h - generated by gen_sintab.py: sin(i*pi/512)*65536, i = 0..256 */")
print("static const int sintab[257] = {")
for i in range(0, 257, 8):
    print("    " + ", ".join(str(x) for x in v[i:i + 8]) + ",")
print("};")
```

  Run `python3 user/mincecraft/gen_sintab.py > user/mincecraft/sintab.h`. `user/mincecraft/fix.h`:

```c
/* fix.h - 16.16 fixed point for Mincecraft (v0.60.192): ring 3 has no FPU. */
#pragma once
typedef int fx;
#define FX_ONE 65536
static inline fx fx_mul(fx a, fx b) { return (fx)(((long long)a * b) >> 16); }
static inline fx fx_div(fx a, fx b) { return (fx)(((long long)a << 16) / b); }
/* Angles in 1024ths of a turn. */
fx fx_sin(int a);
fx fx_cos(int a);
```

  `user/mincecraft/fix.c`:

```c
/* fix.c - sine and cosine from a quarter-wave table (v0.60.192). */
#include "fix.h"
#include "sintab.h"
fx fx_sin(int a) {
    a &= 1023;
    if (a < 256) return sintab[a];
    if (a < 512) return sintab[512 - a];
    if (a < 768) return -sintab[a - 512];
    return -sintab[1024 - a];
}
fx fx_cos(int a) { return fx_sin(a + 256); }
```

- [ ] **Step 5: Blocks.** `user/mincecraft/blocks.h`:

```c
/* blocks.h - what the world is made of (v0.60.192). */
#pragma once
enum { B_AIR, B_CRUST, B_CRUMB, B_PARMESAN, B_CASTIRON, B_BROTH, B_SUGAR,
       B_STALK, B_FLORET, B_PEPPERCORN, B_BUTTER, B_COUNT };
enum { FACE_TOP, FACE_BOTTOM, FACE_SIDE };
typedef struct {
    const char   *name;
    unsigned      base, alt;   /* 0xRRGGBB: the texture's two colours        */
    unsigned char speck;       /* alt where the texel's hash is below this   */
    unsigned char solid;       /* collides and stops rays                    */
    unsigned char hardness;    /* ticks held to break it; 0: never           */
} block_t;
extern const block_t blocks[B_COUNT];
unsigned block_texel(int id, int face, int tx, int ty);   /* tx, ty 0..7 */
```

  `user/mincecraft/blocks.c`:

```c
/* blocks.c - the block table and its 8x8 textures, made from a hash of
 * (block, face, texel) so there is no image data (v0.60.192). */
#include "blocks.h"
const block_t blocks[B_COUNT] = {
    [B_AIR]        = { "air",            0x000000, 0x000000,   0, 0,  0 },
    [B_CRUST]      = { "crust",          0xC8873A, 0x8A5A22,  70, 1,  6 },
    [B_CRUMB]      = { "crumb",          0xEAD9B0, 0xC9B489,  60, 1,  6 },
    [B_PARMESAN]   = { "parmesan",       0xEFE8C8, 0xD4C99A,  90, 1, 20 },
    [B_CASTIRON]   = { "cast iron",      0x2A2A2E, 0x18181B,  80, 1,  0 },
    [B_BROTH]      = { "broth",          0xC8902E, 0xB07A20,  40, 0,  0 },
    [B_SUGAR]      = { "sugar",          0xFAFAF5, 0xDCDCE6,  50, 1,  5 },
    [B_STALK]      = { "broccoli stalk", 0xA9C77A, 0x86A65A,  80, 1, 12 },
    [B_FLORET]     = { "floret",         0x2F6B2A, 0x1D4A1A, 110, 1,  3 },
    [B_PEPPERCORN] = { "peppercorn",     0xEFE8C8, 0x1A1410,  40, 1, 24 },
    [B_BUTTER]     = { "butter",         0xF6DC6B, 0xEBCB52,  30, 1, 10 },
};
static unsigned tex_hash(int id, int face, int tx, int ty) {
    unsigned h = (unsigned)id * 73856093u ^ (unsigned)face * 19349663u
               ^ (unsigned)tx * 83492791u ^ (unsigned)ty * 2654435761u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h & 255;
}
unsigned block_texel(int id, int face, int tx, int ty) {
    if (id == B_CRUST && (face == FACE_BOTTOM || (face == FACE_SIDE && ty >= 2))) id = B_CRUMB;   /* crust on top, crumb inside */
    const block_t *b = &blocks[id];
    if (id == B_STALK && face != FACE_SIDE) {                     /* rings on the cut ends */
        int d = (2 * tx - 7) * (2 * tx - 7) + (2 * ty - 7) * (2 * ty - 7);
        return d >= 18 && d <= 34 ? b->alt : b->base;
    }
    return tex_hash(id, face, tx, ty) < b->speck ? b->alt : b->base;
}
```

- [ ] **Step 6: World.** `user/mincecraft/world.h`:

```c
/* world.h - the block grid (v0.60.192): 128 x 128 columns, 64 high, a byte a block. */
#pragma once
#include "blocks.h"
#define WX 128
#define WZ 128
#define WY 64
#define WCELLS (WX * WZ * WY)
#define SEA 26                     /* broth fills open cells below this */
extern unsigned char *world;       /* (y * WZ + z) * WX + x */
static inline int world_get(int x, int y, int z) {
    if (x < 0 || x >= WX || z < 0 || z >= WZ || y < 0) return B_CASTIRON;
    if (y >= WY) return B_AIR;
    return world[(y * WZ + z) * WX + x];
}
static inline void world_set(int x, int y, int z, int id) {
    if (x >= 0 && x < WX && z >= 0 && z < WZ && y >= 0 && y < WY) world[(y * WZ + z) * WX + x] = (unsigned char)id;
}
int      world_alloc(void);                  /* 0, or -1 */
int      world_height(int x, int z, unsigned seed);
void     world_gen(unsigned seed);
void     world_flat(void);                   /* for the tests: crust at y 10 */
unsigned world_hash(void);                   /* FNV-1a 32 over the grid */
int      world_top(int x, int z);            /* highest solid y, or -1 */
```

  `user/mincecraft/world.c` (keep it in step with `scripts/mincecraft_gen.py`; a comment at the top says so):

```c
/* world.c - the grid and its generation (v0.60.192). scripts/mincecraft_gen.py
 * is this file again in Python for the tests: change both together. */
#include "../ulib.h"
#include "world.h"
unsigned char *world;
int world_alloc(void) {
    void *p = sys_sbrk(WCELLS);
    if (p == (void *)-1) return -1;
    world = (unsigned char *)p;
    return 0;
}
static unsigned hash2(int x, int z, unsigned seed) {
    unsigned h = seed ^ ((unsigned)x * 0x27d4eb2du) ^ ((unsigned)z * 0x165667b1u);
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
    return h;
}
static int noise(int x, int z, int s, unsigned seed) {           /* 0..255, x and z >= 0 */
    int x0 = x / s, z0 = z / s, fx = x % s, fz = z % s;
    int v00 = (int)(hash2(x0, z0, seed) & 255),     v10 = (int)(hash2(x0 + 1, z0, seed) & 255);
    int v01 = (int)(hash2(x0, z0 + 1, seed) & 255), v11 = (int)(hash2(x0 + 1, z0 + 1, seed) & 255);
    int a = v00 * (s - fx) + v10 * fx, b = v01 * (s - fx) + v11 * fx;
    return (a * (s - fz) + b * fz) / (s * s);
}
int world_height(int x, int z, unsigned seed) {                  /* 20..44 solid blocks */
    return 20 + (noise(x, z, 32, seed) * 16 + noise(x, z, 8, seed ^ 0x9e3779b9u) * 8) / 255;
}
void world_gen(unsigned seed) {
    for (int z = 0; z < WZ; z++) for (int x = 0; x < WX; x++) {
        int edge = x == 0 || z == 0 || x == WX - 1 || z == WZ - 1;
        int h = world_height(x, z, seed);
        int top = h < SEA + 2 ? B_SUGAR : B_CRUST;
        for (int y = 0; y < WY; y++) {
            int b;
            if (edge || y == 0) b = B_CASTIRON;
            else if (y < h - 4) {
                unsigned r = hash2(x + z * WX, y, seed ^ 0x51ed27u);
                b = r % 97 == 0 ? B_PEPPERCORN : (y < 12 && r % 211 == 1) ? B_BUTTER : B_PARMESAN;
            }
            else if (y < h - 1) b = top == B_SUGAR ? B_SUGAR : B_CRUMB;
            else if (y == h - 1) b = top;
            else if (y < SEA) b = B_BROTH;
            else b = B_AIR;
            world[(y * WZ + z) * WX + x] = (unsigned char)b;
        }
    }
    for (int z = 3; z < WZ - 3; z++) for (int x = 3; x < WX - 3; x++) {   /* broccoli */
        int h = world_height(x, z, seed);
        if (h < SEA + 2) continue;
        unsigned r = hash2(x, z, seed ^ 0x7f4a7c15u);
        if (r % 60) continue;
        int t = 4 + (int)((r >> 8) % 3), cy;
        for (int y = h; y < h + t; y++) world_set(x, y, z, B_STALK);
        cy = h + t;
        for (int dy = -2; dy <= 2; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy + dz * dz <= 5 && world_get(x + dx, cy + dy, z + dz) == B_AIR)
                world_set(x + dx, cy + dy, z + dz, B_FLORET);
    }
}
void world_flat(void) {
    for (int y = 0; y < WY; y++) for (int z = 0; z < WZ; z++) for (int x = 0; x < WX; x++) {
        int edge = x == 0 || z == 0 || x == WX - 1 || z == WZ - 1;
        world[(y * WZ + z) * WX + x] = (unsigned char)(edge || y == 0 ? B_CASTIRON : y < 10 ? B_PARMESAN : y == 10 ? B_CRUST : B_AIR);
    }
}
unsigned world_hash(void) {
    unsigned h = 2166136261u;
    for (int i = 0; i < WCELLS; i++) { h ^= world[i]; h *= 16777619u; }
    return h;
}
int world_top(int x, int z) {
    for (int y = WY - 1; y >= 0; y--) if (blocks[world_get(x, y, z)].solid) return y;
    return -1;
}
```

  Note the stalk loop in C keeps its `cy` declaration inside the block, and the Python is the same order: rows of z, then x; stalks overwrite, florets only fill air.

- [ ] **Step 7: A first `main.c`** with only `-gen`, `-seed` and `-flat`, enough for the test. Later tasks grow it. Note: Python's `%d` of the seed must equal C's unsigned print, so print the seed as unsigned. ulib's `eprint_int` takes an int, so add a local `eprint_u`:

```c
/* main.c - Mincecraft (v0.60.192): a voxel sandbox in a food world.
 *
 *   cook mincecraft.elf                 play (from a terminal on the desktop)
 *   cook mincecraft.elf -gen -seed N    make world N, print its hash, end
 */
#include "../ulib.h"
#include "world.h"

static void eprint_u(unsigned v) {
    char b[12]; int n = 11; b[n] = 0;
    do { b[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    eprint(b + n);
}
static unsigned parse_u(const char *s) { unsigned v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned)(*s++ - '0'); return v; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

int main(void) {
    char *argv[12];
    int argc = uargv(argv, 12), gen_only = 0, flat = 0, seeded = 0;
    unsigned seed = 0;
    for (int i = 0; i < argc; i++) {
        if (same(argv[i], "-gen")) gen_only = 1;
        else if (same(argv[i], "-flat")) flat = 1;
        else if (same(argv[i], "-seed") && i + 1 < argc) { seed = parse_u(argv[++i]); seeded = 1; }
    }
    if (world_alloc() < 0) { print("Mincecraft: no memory for the world\n"); return 1; }
    if (!seeded) sys_random(&seed, 4);
    if (flat) world_flat(); else world_gen(seed);
    if (gen_only) {
        eprint("[mincecraft] world seed="); eprint_u(seed);
        eprint(" hash="); eprint_u(world_hash());
        eprint(" top="); eprint_u((unsigned)world_top(64, 64));
        eprint("\n");
        return 0;
    }
    print("Mincecraft: play comes in a later version\n");
    return 1;
}
```

  The Python reference's `top` skips broth the same way `world_top` does (broth is not solid), so the two agree.

- [ ] **Step 8: The Makefile.** Add `user/mincecraft.elf` to the end of `USER_ELFS`. After the `user/%.elf` pattern rule (line 120), add:

```make
# Mincecraft (v0.60.192): several files, so a rule of its own; the pattern
# rule's prerequisites plus its sources.
MC_SRC = $(wildcard user/mincecraft/*.c)
user/mincecraft.elf: $(MC_SRC) $(wildcard user/mincecraft/*.h) user/ulib.c user/ulib.h user/user.ld src/syscall_nr.h user/rx.c
	$(CC) $(UCFLAGS) -Wl,-T,user/user.ld -o $@ user/ulib.c $(MC_SRC) $(shell $(CC) -m32 -print-libgcc-file-name)
```

  An explicit rule beats the pattern rule in GNU make, so `user/mincecraft.c` is never looked for. In the disk rule, after the last program's `mcopy` line (scale.elf, around `Makefile:305`), add:

```make
	mcopy -i $(DISK) -o user/mincecraft.elf ::mincecraft.elf
```

  This is the first program copied under a long name; mtools writes the long-name entries itself. `make user && rm disk.img && make disk && mdir -i disk.img ::` must list `mincecraft.elf`. Check `size user/mincecraft.elf` stays under 256 KB (`MAX_ELF`), and do so again at the end of each later task.

- [ ] **Step 9: Run the test to see it pass.** `make user && rm disk.img && make disk && make && ./scripts/mincecraft-gen-test.sh`. Expected: three `ok` lines and PASS. A mismatch means C and Python differ: diff them by printing `world_height` for a few columns from both.

- [ ] **Step 10: Commit** after `./scripts/remote-check.sh` passes. Add `mincecraft-gen-test` to check.sh's list and the v0.60.192 entry ("MINCECRAFT: A WORLD OF FOOD"). CLAUDE.md: add `user/mincecraft/` to the source layout and the ELF to the program list, and note that it is copied under its long name.

```bash
git add Makefile user/mincecraft/ scripts/mincecraft_gen.py scripts/mincecraft-gen-test.sh scripts/check.sh ROADMAP.txt CLAUDE.md
git commit -m "Mincecraft: a world of food (v0.60.192)" -m "Build rule, 16.16 fixed point, the block table, seeded generation checked against Python.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: the ray caster, the window and the speed measurement (v0.60.193)

This is the spec's "measure first" spike, kept because it is the real renderer. **If the idle measurement in Step 6 is under 15 fps, stop and apply the spec's fallbacks** (a 20-block view by setting `VIEW 20`, then `RW 256, RH 192` with the scale-up adjusted) before going on. Record the numbers in the changelog either way.

**Files:**
- Create: `user/mincecraft/ray.h`, `ray.c`, `render.h`, `render.c`
- Modify: `user/mincecraft/main.c` (window, loop, fixed camera, fps)
- Create: `scripts/mincecraft-view-test.sh`
- Modify: `scripts/check.sh`, `ROADMAP.txt`

**Interfaces:**
- Consumes: Task 3's `world_get`, `blocks[]`, `block_texel`, `fx_*`.
- Produces: `ray_t`, `void ray_cast(fx ox, fx oy, fx oz, fx dx, fx dy, fx dz, fx maxt, ray_t *r)`; `cam_t { fx x, y, z; int yaw, pitch; }`; `void cam_forward(const cam_t *c, fx *fx_, fx *fy, fx *fz)`; `int render_alloc(void)`; `void render(const cam_t *c, unsigned *body, int light, unsigned sky)`; `#define BODY_W 640`, `#define BODY_H 480`.

- [ ] **Step 1: Write the failing view test.** `ls scripts/mincecraft-view-test.sh` first. It boots, starts the desktop, opens a terminal (copy `swirl-test.sh`'s preamble verbatim), runs `cook mincecraft.elf -seed 42 -time 600`, waits 6 s, takes `MON:screendump $WORK/noon.ppm`, then closes it with `KEY:esc`. It then reads the PPM in Python. Find the window's body origin from a `KEEP=1` run, as in Task 1, and hard-code it with a comment. The assertions:
  - the body's top row (y 0 to 3) is all the noon sky colour `0x87BEEB` (exact, since the sky is not textured and the camera looks level at a world whose terrain is below y 45);
  - the 2-pixel crosshair is drawn at the body's centre (Task 6 adds the HUD, so in this task only assert the sky and the next point);
  - the body's bottom-centre pixel (320, 470) is not the sky colour (ground is drawn);
  - the serial log has `[mincecraft] window up` and an `[mincecraft] fps=N` line with N >= 5.

  The `-time` flag arrives in Task 7. In this task, main accepts `-time T` and ignores it, and the sky is always noon, so this test needs no change later.

- [ ] **Step 2: Run to see it fail.** Expected FAIL: there is no window yet, because main returns 1.

- [ ] **Step 3: `ray.h` / `ray.c`.**

```c
/* ray.h - a ray through the block grid, Amanatides and Woo (v0.60.193). */
#pragma once
#include "fix.h"
typedef struct {
    int hit;            /* 1: a solid block; 0: sky or out of reach        */
    int x, y, z;        /* the block hit                                   */
    int px, py, pz;     /* the cell before it, where a placed block goes   */
    int axis, step;     /* the face crossed: axis 0 x, 1 y, 2 z; its step  */
    fx  t;              /* how far along d                                 */
    fx  u, v;           /* the hit point across the face, 0..FX_ONE-1      */
    fx  broth_t;        /* where the ray entered broth, or -1              */
} ray_t;
void ray_cast(fx ox, fx oy, fx oz, fx dx, fx dy, fx dz, fx maxt, ray_t *r);
```

```c
/* ray.c - the DDA (v0.60.193). Each step goes to the nearest cell
 * boundary on whichever axis is closest. A direction component near zero
 * would overflow 1/d in 16.16, so its step is held at BIG and never taken. */
#include "ray.h"
#include "world.h"
#define BIG (1 << 30)
static inline fx tdelta(fx d) { if (d < 0) d = -d; return d < 64 ? BIG : (fx)((1LL << 32) / d); }
static inline fx tfirst(fx o, fx d, fx td) {
    if (td == BIG) return BIG;
    fx frac = d > 0 ? FX_ONE - (o & 0xFFFF) : (o & 0xFFFF);
    return (fx)(((long long)frac * td) >> 16);
}
void ray_cast(fx ox, fx oy, fx oz, fx dx, fx dy, fx dz, fx maxt, ray_t *r) {
    int cx = ox >> 16, cy = oy >> 16, cz = oz >> 16;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
    fx tdx = tdelta(dx), tdy = tdelta(dy), tdz = tdelta(dz);
    fx tmx = tfirst(ox, dx, tdx), tmy = tfirst(oy, dy, tdy), tmz = tfirst(oz, dz, tdz);
    r->hit = 0;
    r->broth_t = world_get(cx, cy, cz) == B_BROTH ? 0 : -1;
    for (;;) {
        int px = cx, py = cy, pz = cz, axis;
        fx t;
        if (tmx <= tmy && tmx <= tmz) { t = tmx; cx += sx; tmx += tdx; axis = 0; }
        else if (tmy <= tmz)          { t = tmy; cy += sy; tmy += tdy; axis = 1; }
        else                          { t = tmz; cz += sz; tmz += tdz; axis = 2; }
        if (t > maxt) return;
        if (cy >= WY && sy > 0) return;                       /* above the world, rising: sky */
        int b = world_get(cx, cy, cz);
        if (b == B_BROTH) { if (r->broth_t < 0) r->broth_t = t; continue; }
        if (!blocks[b].solid) continue;
        r->hit = 1; r->x = cx; r->y = cy; r->z = cz; r->px = px; r->py = py; r->pz = pz;
        r->axis = axis; r->step = axis == 0 ? sx : axis == 1 ? sy : sz; r->t = t;
        fx hx = ox + fx_mul(dx, t), hy = oy + fx_mul(dy, t), hz = oz + fx_mul(dz, t);
        if (axis == 0)      { r->u = hz & 0xFFFF; r->v = hy & 0xFFFF; }
        else if (axis == 1) { r->u = hx & 0xFFFF; r->v = hz & 0xFFFF; }
        else                { r->u = hx & 0xFFFF; r->v = hy & 0xFFFF; }
        return;
    }
}
```

- [ ] **Step 4: `render.h` / `render.c`.**

```c
/* render.h - the view (v0.60.193). */
#pragma once
#include "fix.h"
#define BODY_W 640
#define BODY_H 480
typedef struct { fx x, y, z; int yaw, pitch; } cam_t;   /* the eye; angles in 1024ths */
void cam_forward(const cam_t *c, fx *dx, fx *dy, fx *dz);
int  render_alloc(void);                                 /* 0, or -1 */
/* light: 0..256 (256 noon); sky: 0xRRGGBB */
void render(const cam_t *c, unsigned *body, int light, unsigned sky);
```

```c
/* render.c - one ray a pixel at 320x240, doubled into the 640x480 body
 * (v0.60.193). Yaw 0 looks along +z; right is +x there. Faces are shaded by
 * axis, the last 8 blocks fade into the sky, broth is a half blend. */
#include "../ulib.h"
#include "render.h"
#include "ray.h"
#include "world.h"
#define RW 320
#define RH 240
#define VIEW 24                          /* blocks */
#define FOG0 (VIEW - 8)
#define KPIX 287                         /* tan(35 degrees) / 160, 16.16: a 70-degree view */
static unsigned *rbuf;
int render_alloc(void) {
    void *p = sys_sbrk(RW * RH * 4);
    if (p == (void *)-1) return -1;
    rbuf = (unsigned *)p;
    return 0;
}
static unsigned shade(unsigned c, int k) {               /* k/256 */
    unsigned r = ((c >> 16 & 255) * (unsigned)k) >> 8, g = ((c >> 8 & 255) * (unsigned)k) >> 8, b = ((c & 255) * (unsigned)k) >> 8;
    return r << 16 | g << 8 | b;
}
static unsigned mix(unsigned a, unsigned b, int k) {     /* k/256 of b */
    int ra = a >> 16 & 255, ga = a >> 8 & 255, ba = a & 255, rb = b >> 16 & 255, gb = b >> 8 & 255, bb = b & 255;
    return (unsigned)(ra + ((rb - ra) * k >> 8)) << 16 | (unsigned)(ga + ((gb - ga) * k >> 8)) << 8 | (unsigned)(ba + ((bb - ba) * k >> 8));
}
void cam_forward(const cam_t *c, fx *dx, fx *dy, fx *dz) {
    fx sy = fx_sin(c->yaw), cy = fx_cos(c->yaw), sp = fx_sin(c->pitch), cp = fx_cos(c->pitch);
    *dx = fx_mul(sy, cp); *dy = sp; *dz = fx_mul(cy, cp);
}
void render(const cam_t *c, unsigned *body, int light, unsigned sky) {
    fx sy = fx_sin(c->yaw), cy = fx_cos(c->yaw), sp = fx_sin(c->pitch), cp = fx_cos(c->pitch);
    fx fx_ = fx_mul(sy, cp), fy = sp, fz = fx_mul(cy, cp);       /* forward */
    fx rx = cy, rz = -sy;                                         /* right (ry 0) */
    fx ux = -fx_mul(sy, sp), uy = cp, uz = -fx_mul(cy, sp);       /* up */
    unsigned broth = shade(blocks[B_BROTH].base, light);
    for (int py = 0; py < RH; py++) {
        fx v = (RH / 2 - py) * KPIX, h0 = -(RW / 2) * KPIX;
        fx dx = fx_ + fx_mul(ux, v) + fx_mul(rx, h0), dy = fy + fx_mul(uy, v), dz = fz + fx_mul(uz, v) + fx_mul(rz, h0);
        fx stx = fx_mul(rx, KPIX), stz = fx_mul(rz, KPIX);
        for (int px = 0; px < RW; px++, dx += stx, dz += stz) {
            ray_t r;
            unsigned col;
            ray_cast(c->x, c->y, c->z, dx, dy, dz, VIEW << 16, &r);
            if (!r.hit) col = sky;
            else {
                int b = world_get(r.x, r.y, r.z);
                int face = r.axis == 1 ? (r.step < 0 ? FACE_TOP : FACE_BOTTOM) : FACE_SIDE;
                int tx = r.u >> 13, ty = face == FACE_SIDE ? 7 - (r.v >> 13) : r.v >> 13;
                int k = face == FACE_TOP ? 256 : face == FACE_BOTTOM ? 128 : r.axis == 0 ? 205 : 166;
                col = shade(block_texel(b, face, tx, ty), k * light >> 8);
                if (r.t > (FOG0 << 16)) { int f = (r.t - (FOG0 << 16)) >> 11; col = mix(col, sky, f > 256 ? 256 : f); }
            }
            if (r.broth_t >= 0) col = mix(col, broth, 128);
            rbuf[py * RW + px] = col;
        }
    }
    for (int py = 0; py < RH; py++) {                            /* doubled */
        unsigned *a = body + (2 * py) * BODY_W, *b2 = a + BODY_W;
        const unsigned *s = rbuf + py * RW;
        for (int px = 0; px < RW; px++) { unsigned c2 = s[px]; a[2 * px] = a[2 * px + 1] = b2[2 * px] = b2[2 * px + 1] = c2; }
    }
}
```

  Fog spans exactly 8 blocks, so `>> 11` turns `(t - FOG0)` (8 << 16 at most) into 0..256. If Step 6 forces `VIEW` down, FOG0 follows, and the span stays 8.

- [ ] **Step 5: The window loop in `main.c`.** Grow main: after the world, skip the `-gen` return, open the window, allocate the 640x480 body with `sys_sbrk(BODY_W * BODY_H * 4)` and `render_alloc()`, and place a fixed camera: x, z at the centre column + 0.5, eye at `(world_top(64,64) + 1) << 16` + 1.6 blocks (104858), yaw 0, pitch 0. Then loop: drain events (`WEV_CLOSE` or `WEV_KEY` 27 ends it), render with `light 256` and `sky 0x87BEEB`, `sys_win_put`, count frames, and every 500 ticks print `[mincecraft] fps=N` (frames * 100 / ticks elapsed). On `sys_win_open` failing, print `Mincecraft could not open its window (is the countertop up?)` and return 1. A ring-3 program cannot tell "no desktop" from "refused", so one message covers both (see spec self-review, below). After opening, `eprint("[mincecraft] window up\n")`. Accept and ignore `-time T` for now.

```c
    int win = sys_win_open(BODY_W, BODY_H, "Mincecraft");
    if (win < 0) { print("Mincecraft could not open its window (is the countertop up?)\n"); return 1; }
    unsigned *body = (unsigned *)sys_sbrk(BODY_W * BODY_H * 4);
    if (body == (unsigned *)-1 || render_alloc() < 0) { print("Mincecraft: no memory for the screen\n"); return 1; }
    eprint("[mincecraft] window up\n");
    cam_t cam = { (64 << 16) + FX_ONE / 2, ((world_top(64, 64) + 1) << 16) + 104858, (64 << 16) + FX_ONE / 2, 0, 0 };
    unsigned fps_t = sys_ticks();
    int frames = 0;
    for (;;) {
        uwinev_t ev;
        int g, quit = 0;
        while ((g = sys_win_event(win, &ev, 0)) == 1)
            if (ev.type == WEV_CLOSE || (ev.type == WEV_KEY && ev.key == 27)) quit = 1;
        if (g < 0 || quit) break;
        render(&cam, body, 256, 0x87BEEB);
        sys_win_put(win, body);
        frames++;
        unsigned now = sys_ticks();
        if (now - fps_t >= 500) { eprint("[mincecraft] fps="); eprint_u((unsigned)(frames * 100 / (int)(now - fps_t))); eprint("\n"); frames = 0; fps_t = now; }
    }
    eprint("[mincecraft] bye\n");
    sys_win_close(win);
    return 0;
```

- [ ] **Step 6: Measure.**
  - `make user && rm disk.img && make disk && make run`, log in, open a terminal, `cook mincecraft.elf -seed 42`, wait 15 s, close it.
  - Read the fps lines from `/tmp/soupos-serial.log`, and the countertop's heap line for the 640x480 window (`asks for a 640x480 window, heap N KB free`).
  - Record both, plus the countertop's puts/drawn report (`countertop.c:497`, printed when the window closes), in the changelog.
  - If fps is under 15, apply the fallbacks in order and re-measure. If heap free is under 2 MB at open, say so in the changelog and add an item to `docs/next-steps.md`.
  - Also look at the picture: crust tops, crumb sides, parmesan, broth, broccoli. Take a screenshot (`~/Documents/Screenshot_*.png`) for the commit description.

- [ ] **Step 7: Run the view test to see it pass.** `./scripts/mincecraft-view-test.sh`. Expected: PASS.

- [ ] **Step 8: Commit** after `./scripts/remote-check.sh`. Add `mincecraft-view-test` to check.sh's list and the v0.60.193 entry ("MINCECRAFT: THE VIEW"), with the measured fps and heap.

```bash
git add user/mincecraft/ scripts/mincecraft-view-test.sh scripts/check.sh ROADMAP.txt
git commit -m "Mincecraft: the view (v0.60.193)" -m "A ray a pixel at 320x240 in 16.16, doubled into a 640x480 window; measured N fps idle under KVM.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

  Put the measured number in place of N.

---

### Task 5: the player, controls and the fixed tick (v0.60.194)

**Files:**
- Create: `user/mincecraft/player.h`, `player.c`
- Modify: `user/mincecraft/main.c` (input, tick loop, `-script`, `-flat` in play)
- Create: `scripts/mincecraft-play-test.sh`
- Modify: `scripts/check.sh`, `ROADMAP.txt`

**Interfaces:**
- Consumes: `sys_win_keys`, `key_held`, `SC_*` (Task 1); `ray_cast`, `cam_t`, `cam_forward` (Task 4).
- Produces: `player_t`, `input_t`, `void player_spawn(player_t *)`, `int player_tick(player_t *, const input_t *, const ray_t *target)` (returns the id of a block it broke, 0 none; breaking lands in Task 6, so in this task it returns 0), `void player_eye(const player_t *, cam_t *)`, `P_HW P_H P_EYE`.

- [ ] **Step 1: Write the failing play test.** `ls scripts/mincecraft-play-test.sh` first. It uses `cook mincecraft.elf -flat -script -world /PLAY.WLD`. The flat world has crust at y 10, so the player spawns with feet at y 11, at x = z = 64.5. `-script` prints after every tick:

```
[mincecraft] p <x> <y> <z> <yaw> <pitch> <ground>
```

  These are the raw 16.16 integers and angle units. The script, after the terminal preamble and the window opening (`WAIT:4`), sends these, each followed by `WAIT:2`:
  1. `KEY:w 1000`: walk forward a second.
  2. `KEY:spc 100`: one jump.
  3. Click the terminal window, then `KEY:w 1000`: no movement (focus elsewhere).
  4. Click the game's body, then `KEY:esc`: quit.

  Python assertions over the `p` lines:
  - Before any key: y == 11 << 16 (720896) and ground == 1.
  - Phase 1: z rose by between 3.0 and 5.5 blocks (the sendkey hold and tick timing are approximate). x unchanged. y stayed 720896 throughout.
  - Phase 2: the highest y over the jump is between 1.1 and 1.5 blocks above 720896, and it ends back at 720896 with ground 1.
  - Phase 3: z unchanged from the end of phase 2 (the focus moved; Review Focus 2).
  - The log ends with `[mincecraft] bye`. Esc reached the game (Review Focus 1). The `saved` marker comes in Task 8.

  Split the phases by the count of `p` lines between markers. The simplest way is to have main print `[mincecraft] mark` on `WEV_KEY` `m`, and type `TYPE:m` between phases. Then the Python splits the `p` lines at each `mark`.

- [ ] **Step 2: Run to see it fail.** Expected FAIL: there are no `p` lines yet.

- [ ] **Step 3: `player.h` / `player.c`.**

```c
/* player.h - the cook in the world (v0.60.194). */
#pragma once
#include "fix.h"
#include "ray.h"
#include "render.h"
#include "blocks.h"
#define P_HW  19661          /* half the box's width, 0.3  */
#define P_H   117965         /* its height, 1.8            */
#define P_EYE 104858         /* the eye above the feet, 1.6 */
typedef struct {
    fx x, y, z;              /* the feet: the box's bottom centre */
    fx vy;
    int yaw, pitch;          /* 1024ths of a turn; pitch -227..227 (80 degrees) */
    int ground;
    unsigned short inv[B_COUNT];
    int slot;                /* the hotbar's chosen slot, 0..8 */
    int bx, by, bz, bn;      /* the block being broken, and ticks so far */
} player_t;
typedef struct { int fwd, side, turn, tilt, jump, brk; } input_t;   /* -1, 0, 1; jump and brk 0/1 */
void player_spawn(player_t *p);
int  player_tick(player_t *p, const input_t *in, const ray_t *target);
void player_eye(const player_t *p, cam_t *c);
```

```c
/* player.c - movement at a fixed 20 ticks a second (v0.60.194): walking,
 * gravity, jumping, broth, and collision one axis at a time. No move is a
 * block or more in a tick, so the box can only newly touch its edge cells. */
#include "player.h"
#include "world.h"
#define WALK  14090          /* 0.215 blocks a tick, 4.3 a second */
#define GRAV  5243           /* 0.08 a tick, each tick */
#define JUMP  27525          /* 0.42: about 1.3 blocks up */
#define TERM  58982          /* 0.9 a tick at most, falling */
#define SWIM  6554           /* 0.1: space in broth */
#define LOOK  8              /* angle units a tick */
static int solid_at(int x, int y, int z) { return blocks[world_get(x, y, z)].solid; }
static int box_hits(fx x, fx y, fx z) {
    int x0 = (x - P_HW) >> 16, x1 = (x + P_HW - 1) >> 16, y0 = y >> 16, y1 = (y + P_H - 1) >> 16;
    int z0 = (z - P_HW) >> 16, z1 = (z + P_HW - 1) >> 16;
    for (int yy = y0; yy <= y1; yy++) for (int zz = z0; zz <= z1; zz++) for (int xx = x0; xx <= x1; xx++)
        if (solid_at(xx, yy, zz)) return 1;
    return 0;
}
static void move_x(player_t *p, fx d) {
    fx n = p->x + d;
    if (!d) return;
    if (!box_hits(n, p->y, p->z)) { p->x = n; return; }
    p->x = d > 0 ? ((((n + P_HW - 1) >> 16) << 16) - P_HW) : (((((n - P_HW) >> 16) + 1) << 16) + P_HW);
}
static void move_z(player_t *p, fx d) {
    fx n = p->z + d;
    if (!d) return;
    if (!box_hits(p->x, p->y, n)) { p->z = n; return; }
    p->z = d > 0 ? ((((n + P_HW - 1) >> 16) << 16) - P_HW) : (((((n - P_HW) >> 16) + 1) << 16) + P_HW);
}
static void move_y(player_t *p, fx d) {
    fx n = p->y + d;
    if (!box_hits(p->x, n, p->z)) { p->y = n; p->ground = 0; return; }
    if (d < 0) { p->y = ((n >> 16) + 1) << 16; p->ground = 1; }
    else p->y = (((n + P_H - 1) >> 16) << 16) - P_H;
    p->vy = 0;
}
void player_spawn(player_t *p) {
    int x = WX / 2, z = WZ / 2;
    while (x < WX - 2 && world_get(x, world_top(x, z) + 1, z) == B_BROTH) x++;   /* a dry column */
    for (int i = 0; i < (int)sizeof(*p); i++) ((char *)p)[i] = 0;
    p->x = (x << 16) + FX_ONE / 2; p->z = (z << 16) + FX_ONE / 2;
    p->y = (world_top(x, z) + 1) << 16;
    p->ground = 1; p->bx = -1;
}
void player_eye(const player_t *p, cam_t *c) { c->x = p->x; c->y = p->y + P_EYE; c->z = p->z; c->yaw = p->yaw; c->pitch = p->pitch; }
int player_tick(player_t *p, const input_t *in, const ray_t *tg) {
    (void)tg;                                                     /* breaking: Task 6 */
    p->yaw = (p->yaw + in->turn * LOOK) & 1023;
    p->pitch += in->tilt * LOOK;
    if (p->pitch > 227) p->pitch = 227;
    if (p->pitch < -227) p->pitch = -227;
    int wet = world_get(p->x >> 16, (p->y + FX_ONE / 2) >> 16, p->z >> 16) == B_BROTH;
    fx sp = wet ? WALK / 2 : WALK, s = fx_sin(p->yaw), c = fx_cos(p->yaw);
    move_x(p, fx_mul(s, in->fwd * sp) + fx_mul(c, in->side * sp));
    move_z(p, fx_mul(c, in->fwd * sp) - fx_mul(s, in->side * sp));
    if (wet) { p->vy -= GRAV / 2; if (in->jump) p->vy = SWIM; if (p->vy < -SWIM) p->vy = -SWIM; }
    else { p->vy -= GRAV; if (in->jump && p->ground) p->vy = JUMP; if (p->vy < -TERM) p->vy = -TERM; }
    move_y(p, p->vy);
    return 0;
}
```

  Check right and left: at yaw 0, D (side +1) moves along +x, which `render`'s right vector also points to. A quick manual run confirms D strafes toward the screen's right. If it does not, flip the sign of `side` in main, not in the maths.

- [ ] **Step 4: main's loop becomes the game loop.** Replace the fixed camera with a `player_t`, built by `player_spawn` after the world. Each pass of the loop:
  1. Drain the events. `WEV_CLOSE` or key 27 quits, `m` prints `[mincecraft] mark`, and a held left button is tracked for Task 6 (`WEV_DOWN` sets `lbtn`, `WEV_UP` clears it).
  2. Read `sys_win_keys` (on -1, treat it as all zeros) and build `input_t`: fwd = W - S, side = D - A, turn = RIGHT - LEFT, tilt = UP - DOWN, jump = SPACE, brk = F or `lbtn`.
  3. Run the ticks owed, at most four, then drop the rest. Each tick: target = `ray_cast` from the eye along `cam_forward` with maxt `5 << 16`; `player_tick`; in `-script` mode, print the `p` line.
  4. Render from `player_eye`.

```c
        unsigned now = sys_ticks();
        for (int n = 0; now - last >= 5 && n < 4; n++, last += 5) {
            player_eye(&pl, &cam);
            cam_forward(&cam, &dx, &dy, &dz);
            ray_cast(cam.x, cam.y, cam.z, dx, dy, dz, 5 << 16, &tg);
            player_tick(&pl, &in, &tg);
            if (script) {
                eprint("[mincecraft] p "); eprint_int(pl.x); eprint(" "); eprint_int(pl.y); eprint(" "); eprint_int(pl.z);
                eprint(" "); eprint_int(pl.yaw); eprint(" "); eprint_int(pl.pitch); eprint(" "); eprint_int(pl.ground); eprint("\n");
            }
        }
        if (now - last >= 5) last = now;                         /* too far behind: let it go */
```

  Declare `fx dx, dy, dz; ray_t tg = {0}; unsigned last = sys_ticks();` before the loop and `input_t in` inside it. Also take `-world PATH` and `-script` in the argument loop now (the path is stored for Task 8).

- [ ] **Step 5: Run the play test to see it pass.** `make user && rm disk.img && make disk && ./scripts/mincecraft-play-test.sh`. Expected: PASS. Run the view test again, since spawn now places the eye through `player_eye`. The numbers are unchanged: the same column + 0.5 and eye height.

- [ ] **Step 6: Commit** after `./scripts/remote-check.sh`. Add `mincecraft-play-test` to the list and the v0.60.194 entry ("MINCECRAFT: WALKING").

```bash
git add user/mincecraft/ scripts/mincecraft-play-test.sh scripts/check.sh ROADMAP.txt
git commit -m "Mincecraft: walking (v0.60.194)" -m "The player at 20 ticks a second: WASD and arrows through SYS_WIN_KEYS, gravity, jumping, broth, collision.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: breaking, placing, the inventory and the HUD (v0.60.195)

**Files:**
- Modify: `user/mincecraft/player.h`, `player.c`
- Create: `user/mincecraft/hud.h`, `hud.c`
- Modify: `user/mincecraft/main.c`, `scripts/mincecraft-play-test.sh`, `scripts/mincecraft-view-test.sh`, `ROADMAP.txt`

**Interfaces:**
- Consumes: Task 5's `player_t`, `player_tick`, the target `ray_t`.
- Produces: `int player_place(player_t *, const ray_t *, int id)` (1 placed, 0 refused); `int hotbar(const player_t *, int ids[9])` (how many); `void hud_draw(unsigned *body, const player_t *, const ray_t *target, int tod)`.

- [ ] **Step 1: Extend the play test first.** After phase 3 and before Esc, click the game's body back and add the following, each followed by `TYPE:m` to mark it:
  4. `KEY:down 2000`, so pitch reaches -227 (looking straight down).
  5. `KEY:f 1000`. The crust underfoot (hardness 6) breaks: expect `[mincecraft] broke 64 10 Z 1`, where Z is where phase 1 left the player, the cell `z >> 16`. Then expect `[mincecraft] inv 1=1`, and the player's y falls to `10 << 16` (Review Focus 5, breaking underfoot).
  6. `TYPE:r`, placing straight down into the player's own cell. Expect `[mincecraft] place refused` and inv unchanged (Review Focus 5, placing into yourself).
  7. `KEY:up 600` (pitch about -131), then `TYPE:r`. Expect `[mincecraft] placed 64 11 Z+1 1` and `[mincecraft] inv 1=0`.

  Python asserts these in order.

  The view test gains a check: the crosshair (white at body (319..320, 232..247) and (312..327, 239..240), drawn as the pixel XOR 0xFFFFFF over the sky) is present, and the hotbar's frame is at the bottom-centre.

- [ ] **Step 2: Run to see it fail.** Expected FAIL at phase 5, since there is no `broke` marker.

- [ ] **Step 3: Breaking in `player_tick`** (replace the `(void)tg;` line; the function now returns the id broken):

```c
    int broke = 0, b;
    if (in->brk && tg->hit && blocks[b = world_get(tg->x, tg->y, tg->z)].hardness) {
        if (tg->x != p->bx || tg->y != p->by || tg->z != p->bz) { p->bx = tg->x; p->by = tg->y; p->bz = tg->z; p->bn = 0; }
        if (++p->bn >= blocks[b].hardness) {
            world_set(tg->x, tg->y, tg->z, B_AIR);
            if (p->inv[b] < 999) p->inv[b]++;
            p->bn = 0; broke = b;
        }
    } else p->bn = 0;
```

  Put it before the movement, so a block broken underfoot lets the fall start on the same tick, and `return broke;` at the end.

  Placing and the hotbar:

```c
int player_place(player_t *p, const ray_t *tg, int id) {
    if (!tg->hit || id <= 0 || id >= B_COUNT || p->inv[id] == 0 || tg->py >= WY) return 0;
    int c = world_get(tg->px, tg->py, tg->pz);
    if (c != B_AIR && c != B_BROTH) return 0;
    int x0 = (p->x - P_HW) >> 16, x1 = (p->x + P_HW - 1) >> 16, y0 = p->y >> 16, y1 = (p->y + P_H - 1) >> 16;
    int z0 = (p->z - P_HW) >> 16, z1 = (p->z + P_HW - 1) >> 16;
    if (tg->px >= x0 && tg->px <= x1 && tg->py >= y0 && tg->py <= y1 && tg->pz >= z0 && tg->pz <= z1) return 0;
    world_set(tg->px, tg->py, tg->pz, id);
    p->inv[id]--;
    return 1;
}
int hotbar(const player_t *p, int ids[9]) {
    int n = 0;
    for (int id = 1; id < B_COUNT && n < 9; id++) if (p->inv[id]) ids[n++] = id;
    return n;
}
```

  Declare both in `player.h`.

- [ ] **Step 4: main's events.**
  - Keys `1`..`9` set `pl.slot`. `WEV_WHEEL` adds `(int)ev.key` (the notches, down positive) and wraps 0..8.
  - `r` or `q`: `int ids[9], n = hotbar(&pl, ids)`. If `pl.slot < n`, call `player_place(&pl, &tg, ids[pl.slot])` and print `placed X Y Z id` or `place refused`; with nothing in the slot, print `place refused`.
  - After a tick that broke something, print `[mincecraft] broke X Y Z id`, using the target that tick used.
  - After any change to the inventory, print `[mincecraft] inv` and then ` id=count` for each id that changed. Simplest: keep a copy of `inv` and print the ids that differ.
  - The target `tg` used by `r` is the one from the latest tick.

- [ ] **Step 5: `hud.h` / `hud.c`.**

```c
/* hud.h - drawn over the view at full size (v0.60.195). */
#pragma once
#include "player.h"
void hud_draw(unsigned *body, const player_t *p, const ray_t *target, int tod);
```

```c
/* hud.c - the crosshair, the hotbar with counts, the name of the block
 * looked at, and the time (v0.60.195). Text in the kernel's 8x16 font. */
#include "../ulib.h"
#include "hud.h"
#include "world.h"
#include "../../src/font8x16.h"
static void rect(unsigned *b, int x, int y, int w, int h, unsigned c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++)
        if (i >= 0 && j >= 0 && i < BODY_W && j < BODY_H) b[j * BODY_W + i] = c;
}
static void text(unsigned *b, int x, int y, const char *s, unsigned fg) {
    for (; *s; s++, x += 8)
        for (int j = 0; j < 16; j++) for (int i = 0; i < 8; i++)
            if ((font8x16[(unsigned char)*s][j] >> (7 - i)) & 1 && x + i < BODY_W && y + j < BODY_H) b[(y + j) * BODY_W + x + i] = fg;
}
static void num(char *o, int v) { char d[8]; int k = 0, n = 0; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) o[n++] = d[--k]; o[n] = 0; }
void hud_draw(unsigned *b, const player_t *p, const ray_t *tg, int tod) {
    for (int i = -8; i < 8; i++) {                                 /* crosshair: inverted */
        for (int t = 0; t < 2; t++) {
            unsigned *h = &b[(BODY_H / 2 - 1 + t) * BODY_W + BODY_W / 2 + i], *v = &b[(BODY_H / 2 + i) * BODY_W + BODY_W / 2 - 1 + t];
            *h ^= 0xFFFFFF; if (i < -1 || i > 0) *v ^= 0xFFFFFF;
        }
    }
    int ids[9], n = hotbar(p, ids), x0 = BODY_W / 2 - 9 * 44 / 2, y0 = BODY_H - 48;
    for (int s = 0; s < 9; s++) {
        int x = x0 + s * 44;
        rect(b, x, y0, 40, 40, s == p->slot ? 0xFFFFFF : 0x808080);
        rect(b, x + 2, y0 + 2, 36, 36, 0x202020);
        if (s < n) {
            char c[8];
            rect(b, x + 6, y0 + 6, 28, 28, blocks[ids[s]].base);
            num(c, p->inv[ids[s]]);
            text(b, x + 38 - 8 * strlen_(c), y0 + 22, c, 0xFFFFFF);
        }
    }
    if (tg->hit) text(b, 8, 8, blocks[world_get(tg->x, tg->y, tg->z)].name, 0xFFFFFF);
    int m = tod * 1440 / 1200;                                     /* minutes past midnight */
    char clk[6] = { (char)('0' + m / 600), (char)('0' + m / 60 % 10), ':', (char)('0' + m % 60 / 10), (char)('0' + m % 10), 0 };
    text(b, BODY_W - 48, 8, clk, 0xFFFFFF);
}
```

  The crosshair's two strokes overlap in the middle 2x2, so the vertical stroke skips `i` = -1 and 0 so as not to XOR twice. Make the view test's expected pixels match exactly what this draws. In main, call `hud_draw(body, &pl, &tg, tod)` after `render`, with `tod` 600 until Task 7.

- [ ] **Step 6: Run both tests to see them pass.** `./scripts/mincecraft-play-test.sh && ./scripts/mincecraft-view-test.sh`.

- [ ] **Step 7: Commit** after `./scripts/remote-check.sh`, with the v0.60.195 entry ("MINCECRAFT: MINING AND BUILDING").

```bash
git add user/mincecraft/ scripts/mincecraft-play-test.sh scripts/mincecraft-view-test.sh ROADMAP.txt
git commit -m "Mincecraft: mining and building (v0.60.195)" -m "Break by hardness into an inventory, place from the hotbar (never into the cook), the HUD.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: day and night (v0.60.196)

**Files:**
- Modify: `user/mincecraft/main.c`, `scripts/mincecraft-view-test.sh`, `ROADMAP.txt`

**Interfaces:**
- Produces: `static int light_at(int tod)` (77..256), `static unsigned sky_at(int tod)`, the `-time T` flag (start at T and hold the clock there).

- [ ] **Step 1: Extend the view test.** A second run, `cook mincecraft.elf -seed 42 -time 0`, takes `midnight.ppm`. Assert:
  - The top row is the midnight sky `0x0F142D`.
  - A ground pixel away from the HUD (body (320, 400)) has each channel at most 0.31 of the noon picture's same pixel, plus 2 for rounding. The midnight light is 77/256, so the ratio is 0.30.
  - The clock text region differs between the two pictures.

- [ ] **Step 2: Run to see it fail.** Expected FAIL: the sky is still the noon colour.

- [ ] **Step 3: Implement.** The time of day runs 0..1199, one step every 20 game ticks (a 20-minute day). `-time T` sets it and stops it advancing (`hold_time = 1`).

```c
static int light_at(int tod) {                 /* 256 at noon (600), 77 at midnight */
    int dn = tod > 600 ? tod - 600 : 600 - tod;
    return 256 - dn * 179 / 600;
}
static unsigned sky_at(int tod) {              /* 0x87BEEB at noon to 0x0F142D at midnight */
    int k = (256 - light_at(tod)) * 256 / 179;  /* 0 noon .. 256 midnight */
    int r = 0x87 + ((0x0F - 0x87) * k >> 8), g = 0xBE + ((0x14 - 0xBE) * k >> 8), b = 0xEB + ((0x2D - 0xEB) * k >> 8);
    return (unsigned)r << 16 | (unsigned)g << 8 | (unsigned)b;
}
```

  Check the ends: at tod 600, k = 0 and the sky is noon's. At tod 0, light is 77, k = 179 * 256 / 179 = 256, and `(0x0F - 0x87) * 256 >> 8` = -120, so r = 0x0F. Arithmetic right shift of a negative product is what GCC does; the test pins it. In the tick loop: `if (!hold_time && ++tt % 20 == 0) tod = (tod + 1) % 1200;`. Render with `light_at(tod)` and `sky_at(tod)`, and pass `tod` to `hud_draw`.

- [ ] **Step 4: Run to see it pass.** `./scripts/mincecraft-view-test.sh`.

- [ ] **Step 5: Commit** after `./scripts/remote-check.sh`, with the v0.60.196 entry ("MINCECRAFT: SERVICE AND AFTER HOURS").

```bash
git add user/mincecraft/main.c scripts/mincecraft-view-test.sh ROADMAP.txt
git commit -m "Mincecraft: day and night (v0.60.196)" -m "A 20-minute day: the sky and the light follow the clock; -time holds it for tests.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: saving and loading (v0.60.197)

**Files:**
- Create: `user/mincecraft/save.h`, `save.c`
- Modify: `user/mincecraft/main.c`
- Create: `scripts/mincecraft-save-test.sh`
- Modify: `scripts/check.sh`, `ROADMAP.txt`, `CLAUDE.md` (a Mincecraft section: controls, files, flags, markers)

**Interfaces:**
- Consumes: `world`, `world_hash`, `player_t`.
- Produces: `int save_world(const char *path, const player_t *p, unsigned seed, int tod, unsigned *bytes)` (0, or -1); `int load_world(const char *path, player_t *p, unsigned *seed, int *tod)` (0 loaded, -1 no file, -2 damaged).

- [ ] **Step 1: Write the failing save test.** `ls scripts/mincecraft-save-test.sh` first. Four QEMU boots on one disk copy (one at a time).
  - **Boot 1:** `cook mincecraft.elf -flat -world /SAVE.WLD -script`, then `KEY:w 600`, `KEY:down 2000`, `KEY:f 1000`, `TYPE:p` (save), `KEY:right 500`, `KEY:esc` (save again and quit). Capture the last `saved` marker, `[mincecraft] saved bytes=N hash=H x=X y=Y z=Z`, and the last `inv` state.
  - **Boot 2:** `cook mincecraft.elf -world /SAVE.WLD -script`, then `KEY:esc`. Expect `[mincecraft] loaded hash=H x=X y=Y z=Z` equal to boot 1's last save, and the first `p` line at X Y Z.
  - **Between boots 2 and 3** (QEMU stopped), on the host:
    - `mcopy -i disk.img ::SAVE.WLD good.wld`
    - flip byte 100: `python3 -c "import sys;b=bytearray(open(sys.argv[1],'rb').read());b[100]^=1;open(sys.argv[2],'wb').write(b)" good.wld bad.wld`
    - make a truncated copy with `head -c -10`, and an appended copy (`good.wld` plus `JUNK`)
    - `mcopy -o` them in as `/BAD.WLD`, `/SHORT.WLD` and `/LONG.WLD`
  - **Boot 3:** `cook mincecraft.elf -world /BAD.WLD -script`, Esc; then `/SHORT.WLD`, then `/LONG.WLD`. Each must print `[mincecraft] refused <path>`. The fresh world's `world` marker follows, and the game keeps running (`fps=` lines appear before `bye`). This covers Review Focus 3.
  - **Boot 4** (Review Focus 4): on the host, `mcopy` a 3 MB file of zeros in as `/T.WLD`.
    Boot, `cook mincecraft.elf -flat -world /T.WLD -script`. Expect `refused /T.WLD` and a fresh flat world. Then `TYPE:p`, `KEY:esc`.
    That writes a small save over a long file. Then `cook mincecraft.elf -world /T.WLD -script`, `KEY:esc`. Expect `loaded` with the saved hash.
    A tail left from the long file fails the end-of-file check, so `loaded` proves `O_WRITE` truncated it.
  - **Last check,** a run with no `-world`: as the headchef, the default is `/mincecraft.wld`. Run `mdir` to check it exists after a P.

- [ ] **Step 2: Run to see it fail.** Expected FAIL: there is no `saved` marker.

- [ ] **Step 3: `save.h` / `save.c`.**

```c
/* save.h - the world file (v0.60.197). */
#pragma once
#include "player.h"
int save_world(const char *path, const player_t *p, unsigned seed, int tod, unsigned *bytes);  /* 0, or -1 */
int load_world(const char *path, player_t *p, unsigned *seed, int *tod);   /* 0, -1 none, -2 damaged */
```

```c
/* save.c - the world file (v0.60.197), little-endian:
 *   "MINC", version 1, seed, time of day, x y z yaw pitch, B_COUNT counts
 *   (32 bits each), the grid as (count 1..255, id) pairs, then the sum of
 *   every byte before it. Written and read through 4 KB, never whole: the
 *   worst run-length encoding is 2 MB. A file that is short, long, of
 *   another version, or whose sum is wrong is refused. */
#include "../ulib.h"
#include "save.h"
#include "world.h"
#define VERSION 1
static unsigned char io[4096];
static int ion, iofd, ioerr, iolen, iopos;
static unsigned iosum, iototal;
static void flush(void) { if (ion && sys_write(iofd, (const char *)io, ion) != ion) ioerr = 1; ion = 0; }
static void put(unsigned c) { iosum += c & 255; iototal++; io[ion++] = (unsigned char)c; if (ion == (int)sizeof(io)) flush(); }
static void put32(unsigned v) { put(v); put(v >> 8); put(v >> 16); put(v >> 24); }
int save_world(const char *path, const player_t *p, unsigned seed, int tod, unsigned *bytes) {
    iofd = sys_open(path, O_WRITE);
    if (iofd < 0) return -1;
    ion = 0; ioerr = 0; iosum = 0; iototal = 0;
    put('M'); put('I'); put('N'); put('C');
    put32(VERSION); put32(seed); put32((unsigned)tod);
    put32((unsigned)p->x); put32((unsigned)p->y); put32((unsigned)p->z); put32((unsigned)p->yaw); put32((unsigned)p->pitch);
    for (int i = 0; i < B_COUNT; i++) put32(p->inv[i]);
    for (int i = 0; i < WCELLS; ) {
        int b = world[i], n = 1;
        while (i + n < WCELLS && n < 255 && world[i + n] == b) n++;
        put((unsigned)n); put((unsigned)b);
        i += n;
    }
    put32(iosum);
    flush();
    sys_close(iofd);
    if (bytes) *bytes = iototal;
    return ioerr ? -1 : 0;
}
static int get(void) {
    if (iopos == iolen) { iolen = sys_read(iofd, (char *)io, sizeof(io)); iopos = 0; if (iolen <= 0) { iolen = 0; return -1; } }
    unsigned char c = io[iopos++];
    iosum += c;
    return c;
}
static int get32(unsigned *v) {
    unsigned r = 0;
    for (int i = 0; i < 4; i++) { int c = get(); if (c < 0) return -1; r |= (unsigned)c << (8 * i); }
    *v = r;
    return 0;
}
int load_world(const char *path, player_t *p, unsigned *seed, int *tod) {
    unsigned v[8], inv[B_COUNT], want, got;
    iofd = sys_open(path, 0);
    if (iofd < 0) return -1;
    iolen = iopos = 0; iosum = 0;
    int bad = !(get() == 'M' && get() == 'I' && get() == 'N' && get() == 'C');
    for (int i = 0; i < 8 && !bad; i++) if (get32(&v[i]) < 0) bad = 1;
    for (int i = 0; i < B_COUNT && !bad; i++) if (get32(&inv[i]) < 0 || inv[i] > 999) bad = 1;
    if (!bad && (v[0] != VERSION || v[2] >= 1200)) bad = 1;
    for (int i = 0; i < WCELLS && !bad; ) {
        int n = get(), b = get();
        if (n <= 0 || b < 0 || b >= B_COUNT || i + n > WCELLS) { bad = 1; break; }
        for (int k = 0; k < n; k++) world[i + k] = (unsigned char)b;
        i += n;
    }
    if (!bad) { want = iosum; if (get32(&got) < 0 || got != want || get() >= 0) bad = 1; }
    sys_close(iofd);
    if (bad) return -2;
    *seed = v[1]; *tod = (int)v[2];
    player_spawn(p);
    p->x = (fx)v[3]; p->y = (fx)v[4]; p->z = (fx)v[5]; p->yaw = (int)v[6] & 1023; p->pitch = (int)v[7];
    if (p->pitch > 227 || p->pitch < -227) p->pitch = 0;
    if (p->x < FX_ONE || p->x >= (WX - 1) << 16 || p->z < FX_ONE || p->z >= (WZ - 1) << 16 || p->y < FX_ONE || p->y >= WY << 16) return -2;
    for (int i = 0; i < B_COUNT; i++) p->inv[i] = (unsigned short)inv[i];
    p->ground = 0;
    return 0;
}
```

  Find the read-mode constant ulib uses for `sys_open` (0 in `SYS_OPEN`'s comment) and use the named one if there is one.

- [ ] **Step 4: main.** The path is `-world PATH`, else `getenv("HOME")` + `/mincecraft.wld` (no doubled slash when HOME is `/`). With neither, print `Mincecraft: no HOME; give -world PATH` and return 1.

  Before generating, call `load_world`:
  - 0: print `[mincecraft] loaded hash=H x=X y=Y z=Z`.
  - -2: `sys_note("Mincecraft: that world is damaged; a new one")`, print `[mincecraft] refused PATH`, then generate (flat or seeded) and spawn.
  - -1: generate and spawn.

  `-time` overrides the loaded time. Print the `world seed= hash= top=` marker for every new world.

  Save on `p`, on Esc and on `WEV_CLOSE`, and when the window is gone (`g < 0`). Each save prints `[mincecraft] saved bytes=N hash=H x= y= z=`. On failure it calls `sys_note("Mincecraft: could not save")`, prints `[mincecraft] save failed`, and the game goes on.

- [ ] **Step 5: Run to see it pass.** `make user && rm disk.img && make disk && ./scripts/mincecraft-save-test.sh`, then the other three Mincecraft tests again.

- [ ] **Step 6: Docs.** Write the CLAUDE.md Mincecraft section, under the user programs:
  - controls (the spec's table, without right-drag)
  - flags: `-seed -flat -gen -script -time -world`
  - the markers, the file format, the memory budget
  - "killed means unsaved"
  - the four test scripts

  Then add the v0.60.197 entry ("MINCECRAFT: KEEPING THE WORLD"), and put a short "Mincecraft" line in the NIGHT-QUEUE or `docs/next-steps.md` for the spec's "Later" list.

- [ ] **Step 7: Commit** after `./scripts/remote-check.sh` passes.

```bash
git add user/mincecraft/ scripts/mincecraft-save-test.sh scripts/check.sh ROADMAP.txt CLAUDE.md docs/next-steps.md
git commit -m "Mincecraft: keeping the world (v0.60.197)" -m "Save on P, Esc and close to HOME/mincecraft.wld, run-length encoded with a sum; damaged files refused.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Spec self-review notes

Folded into the spec (commit with this plan): one window-refusal message, since a ring-3 program cannot tell "no desktop" from "refused"; `-time T` holds the clock; `-flat`, a test-only world with crust at y 10; `m` prints a mark for the tests.

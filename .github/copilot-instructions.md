# Copilot instructions for soupOS CTF

## Scope and safety
- Make the smallest possible change that fully fixes the task.
- Do not modify files under `solutions/` and never reveal or reuse their contents.
- Keep challenge mechanics intact unless the task explicitly asks to change them.

## Project overview
- This repository contains `soupOS`, a freestanding 32-bit x86 OS in C and NASM.
- Main code lives in `src/`.
- Ring-3 example programs live in `user/`.
- Boot and image assets live in `iso/`.
- Test and automation scripts live in `scripts/`.
- Challenge packaging lives in `challenge/`.
- Tooling helpers live in `tools/`.

## Build, run, and test
Use commands from the repository root.

- Build kernel and ISO: `make`
- Build user ELFs: `make user`
- Build disk image: `make disk`
- Full clean rebuild of disk: `make distclean && make disk`
- Run in QEMU GUI: `make run`
- Run headless serial console: `make run-console`
- Run with AI bridge: `make run-ai`
- Copy WAD into an existing disk image: `make wad`

Challenge build path (required for challenge behavior):
- `make CHALLENGE=1`
- `make CHALLENGE=1 disk`
- `make CHALLENGE=1 run`

Tests and verification:
- Headless smoke gate: `./scripts/smoke-test.sh`
- Keep smoke logs: `KEEP=1 ./scripts/smoke-test.sh`
- Serial console test: `./scripts/console-test.sh`

## Code style and conventions
- Freestanding C with no libc. Use in-tree helpers such as `src/str.c`, `src/doom_libc.c`, kernel allocators, and kernel I/O APIs.
- Follow existing naming: `snake_case` for functions and variables, `UPPER_CASE` for macros/constants.
- Prefer `static` for file-local helpers and state, matching existing files.
- Keep comments concise and technical, in the style already used near edited code.

## Rules for safe changes
- Do not remove critical build flags in `Makefile`, especially `-mgeneral-regs-only` and `-fno-omit-frame-pointer`.
- If you change code under `user/`, rebuild user ELFs and refresh `disk.img` so tests do not use stale binaries.
- After code changes, run targeted checks first, then run `./scripts/smoke-test.sh` as the final gate.
- Avoid unrelated refactors and avoid changing public behavior outside the task.

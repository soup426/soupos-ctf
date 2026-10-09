# Pipes and redirection (M3)

Written 2026-10-06, on the process foundation (v0.8.0) and job control (v0.8.2).

## Why this is M3 without `fork()`

M3 was planned as "fork/exec and pipes". Building it revealed that `fork()` has
no caller here. In Unix, fork exists mostly so a shell can duplicate itself
before exec; soupOS's shell runs *in the kernel*, and `proc_spawn` already
creates a process from nothing. No user program in the tree wants to start
another, and inventing one to justify an address-space copy (or copy-on-write
page tables) is the wrong order.

So M3 ships the half that pays for itself immediately:

```
cook echo.elf hello world | upper.elf | wc.elf
cook cat.elf < README.TXT > COPY.TXT
```

`fork()` stays on the roadmap, and will be worth building the first time a
*user program* needs to spawn one.

## Three pieces

### 1. File descriptors 0, 1 and 2 become bindable

Today `syscall_dispatch` hardcodes them: write to 1 goes to the VGA console,
write to 2 to serial, read from 0 to the keyboard, and only fd >= 3 consults
the process's table. That is why nothing can be redirected.

The rule becomes: **an explicit binding in `proc_t.ufds[fd]` wins; otherwise
the console default applies.** Nothing changes for a program that is run
normally, because nothing binds 0-2 for it.

`usermode_run` currently clears the whole fd table when it loads a program. It
must clear from 3 up instead, or it would wipe the bindings the shell just set.
On exit it still closes everything, which is what closes a pipe end and lets
the next stage see EOF.

### 2. A pipe is a VFS node

`vfs_pipe(&rd, &wr)` returns two ordinary `vfs_node_t` handles over one shared
ring buffer (4 KB), closed with `vfs_close` like anything else. `vfs_node_t`
gains a `void *priv` for the shared state, which the FAT and /dev backends
leave NULL.

Semantics, which are the whole point:

- **read** blocks while the pipe is empty and a writer still exists; returns 0
  (EOF) once the buffer is empty and the last writer has closed.
- **write** blocks while the pipe is full and a reader still exists; returns -1
  once every reader has closed, rather than blocking forever.
- Both wake the other side after moving bytes.

Blocking enables interrupts first and restores the caller's state afterwards,
the same requirement `proc_take_stop` has and for the same reason: syscalls
arrive through an interrupt gate with IF clear, and `task_block_on` ends in
`hlt` when nothing else is runnable.

Both also give up when the running process has a kill pending, so a program
parked on a pipe that nobody will ever write to is still killable with Ctrl-C.
Without that, `cook cat.elf` with no input would be an unkillable process.

### 3. The shell runs pipelines

`cook` takes a whole pipeline rather than one program:

- split the line on `|` into at most `PIPE_MAX_STAGES` (4) stages,
- `< FILE` on the first stage and `> FILE` on the last are opened as files,
- a trailing `&` still backgrounds, and applies to the pipeline as a whole,
- stage *i*'s stdout is a pipe whose read end is stage *i+1*'s stdin,
- the last stage gets the terminal (foreground), and the shell waits for every
  stage, reporting the last one's exit code.

Ownership is simple on purpose: the shell hands each end to exactly one
process and keeps no reference, so the ends close when those processes exit.
If a spawn fails partway, the shell closes what it still holds and kills what
it already started.

## Programs, so there is something to pipe

- `cat.elf` with no argument becomes a filter: stdin to stdout. With an
  argument it still copies a file, as now.
- `upper.elf` (new): stdin to stdout, uppercased.
- `wc.elf` (new): counts lines, words and bytes on stdin, prints the counts,
  and emits a `WCOUT` marker to serial so the gate can read the result.

## Testing

Added to the smoke gate:

1. `cook echo.elf hello pipes | wc.elf` reports 1 line, 2 words.
2. A three-stage pipeline (`echo | upper | wc`) runs, proving the wiring is not
   special-cased for two.
3. `cook echo.elf ... > FILE.TXT` then `cook cat.elf FILE.TXT` reads back what
   was written, proving `>` reached the filesystem.
4. A program parked on an empty pipe is killable (`cook cat.elf &`, then
   `kill %`), which is the case the kill-awareness exists for.

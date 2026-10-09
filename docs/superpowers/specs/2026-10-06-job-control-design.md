# Job control (M2)

Written 2026-10-06, on top of the process foundation shipped in v0.8.0.

## What this adds

Ctrl-Z stops the foreground program and gives the shell back. `jobs` lists it
as stopped, `bg` resumes it in the background, `fg` resumes it in the
foreground and waits for it again. That is the whole feature.

## What it deliberately does not add

- **Process groups.** A job is one process until pipes exist (M3). A group of
  one is bookkeeping with no behaviour, so it waits for the thing that needs it.
- **User-space signal handlers.** Delivering a signal to ring 3 means building
  a frame on the user stack, a sigreturn path, and a disposition table. The
  dispositions here are all taken by the kernel: stop, continue, terminate.
  Nothing a program can catch, which also means no new ABI to get wrong.
- **SIGTSTP from a program**, job specs like `%1` as distinct from pids (`kill
  %` already means "the newest job"), or `disown`.

## The stopped state

`proc_state_t` gains `PROC_STOPPED`. A stopped process is alive, holds its
address space, and its task is `TASK_BLOCKED`, so it costs nothing but memory.

```
          Ctrl-Z / proc_flag_stop          fg, bg
 RUNNING ───────────────────────────► STOPPED ──────► RUNNING
    │                                    │
    │ exit / fault / kill                │ kill
    ▼                                    ▼
 ZOMBIE                              (continued, then unwinds at its safe point)
```

## Where a process stops

The same three safe points the kill unwind already uses, and for the same
reason: the kernel holds no locks there, so parking the task cannot strand a
mutex.

1. `syscall_dispatch` entry,
2. `syscall_dispatch` exit,
3. an IRQ that interrupted ring-3 code (`(cs & 3) == 3`).

Point 3 is what lets a program that makes no syscalls at all be stopped, the
same way it is what lets `hog.elf` be killed.

Parking enables interrupts first. The syscall gate and the IRQ gate are both
interrupt gates, so IF is clear on entry, and `task_block_on` ends in `hlt`
when nothing else is runnable: parking with interrupts off would be a
permanent sleep. `SYS_YIELD` already sets this precedent.

## Ctrl-Z

Handled in the keyboard IRQ exactly like Ctrl-C, and for the same reason: with
a foreground program running, the shell is blocked in `proc_wait` and is
reading no keys, so the terminal driver has to raise it. `0x1A` flags the
foreground process and is swallowed. With no foreground process it falls
through to the shell, which currently ignores it.

## proc_wait has to report two outcomes

Today it returns an exit code and frees the slot. A stopped child also wakes
the parent, but the slot must survive, so the signature becomes:

```c
/* 1 = exited (code written, slot collected), 0 = stopped (slot kept),
   -1 = no such process. */
int proc_wait(uint32_t pid, int *code);
```

The shell prints `[pid] stopped  NAME` and returns to the prompt.

## Killing a stopped process

`proc_kill` must continue it as well as flag it, otherwise the kill is noticed
only when something else wakes it, which may be never. The parked loop
therefore exits on either a continue or a pending kill, and the safe point it
returns to takes the kill as usual.

## Shell surface

- `fg [pid]` / `bg [pid]`, defaulting to the newest job.
- `jobs` gains a `T` state letter for stopped, matching `ps`-style convention.
- `proc_most_recent()` counts stopped jobs as live, so `kill %` and a bare
  `fg` can reach them.

## Testing

Added to the smoke gate, which runs `marker.elf` because it prints a tagged
line every 100 ms: its output *is* the evidence of whether it is running.

1. `cook marker.elf Z` in the foreground, Ctrl-Z: its lines must stop, and a
   following shell command must run, proving the shell got the terminal back.
2. `bg`: the same process's lines must resume, with its pid unchanged.
3. `fg` on a stopped job, then let it finish, and check the exit is reported.
4. `kill %` on a stopped job must actually end it.

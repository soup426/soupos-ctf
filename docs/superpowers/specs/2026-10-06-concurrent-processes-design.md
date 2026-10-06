# Concurrent processes (M1)

Written 2026-10-06. First milestone of the process work that follows the
per-process address spaces landed in `86c9060`.

## What forces this

`exec_elf` is a one-shot coroutine swap, not a process. One ring-3 program
runs at a time, synchronously, on the shell's own task. Three things in the
tree say so out loud:

- `tss_set_esp0()` is defined in `gdt.c` and **never called**. `tss.esp0` is
  set once at boot to a single static `kstack[16384]`, so every ring3 to ring0
  trap from any program lands on the same stack.
- `kernel_resume_esp` in `usermode_asm.asm` is one word in `.bss`: a single
  global resume point.
- Every piece of per-run state in `usermode.c` is file-scope: `g_exit_code`,
  `g_user_active`, `mapped[]`/`nmapped`, `g_user_brk`, `g_ufds[]`,
  `g_user_args`, and the fault record.

The concrete failure mode with two programs: the timer fires while program A
is in ring 3, the CPU loads `esp0` and pushes A's trap frame onto `kstack`,
`sched_preempt` switches away with that frame still live, then program B makes
a syscall, the CPU loads the same `esp0`, and B's frame lands on top of A's.
A never returns anywhere sane.

A second consequence, visible today: because `cmd_cook` calls `exec_elf` on
the shell task, a program that never exits wedges the shell forever.

## The model: a process is a kernel task

Each program gets a `task_t` of its own, spawned by `proc_spawn`, whose entry
loads and runs the ELF. `proc_t` holds what used to be file-scope in
`usermode.c` and hangs off `task_t`. The pid is the task id, so `ps` keeps
showing one kind of thing.

Chosen over a separate process table with its own scheduler, which would
duplicate the ready list, wait queues and reaper that `task.c` already has,
and over a resumable `exec_elf` multiplexed by the shell, which needs the same
`esp0` work anyway and puts programs back on cooperative-only scheduling where
one non-yielding program starves the rest.

## The ring-0 stack invariant

`task_t` gains `uint32_t user_resume_esp`, zero for a task that has never
entered ring 3.

- `user_mode_enter` records the esp captured after its own pushes on the
  current task, and passes the same value to `tss_set_esp0()`.
- `task_yield` updates `tss.esp0` from the incoming task on every switch, and
  falls back to the boot `kstack` top when the incoming task has no resume esp.
- `user_mode_exit` reads the value back off the current task.

**Invariant: whenever `current` changes, `tss.esp0` follows it.**

`esp0` is the resume esp and not the task's stack top because `user_mode_enter`
leaves four callee-saved registers on the task's kernel stack for the whole
time ring 3 runs. Pointed at the stack top, the CPU's trap frame would land on
top of that saved frame and destroy the only record of how to get back into
`exec_elf`. Captured at the `iret`, every trap from ring 3 pushes below it.

The nested case falls out of the same rule: a ring-3 task traps, yields inside
the syscall (`SYS_YIELD` does), another ring-3 task runs, and each switch moves
`esp0` with it. The "safe because only one user program runs at a time" caveat
in the `SYS_YIELD` comment becomes true by construction.

## proc_t and the process table

New `src/proc.c` / `src/proc.h`. A fixed array of 8 slots, no allocation:

```c
typedef enum { PROC_FREE, PROC_RUNNING, PROC_ZOMBIE } proc_state_t;

typedef struct proc {
    uint32_t      pid;              /* == owning task id while alive      */
    char          name[32];         /* program path, for ps/jobs          */
    proc_state_t  state;
    int           exit_code;
    int           killed;           /* pending kill, unwound at a safe point */
    int           in_user;          /* in ring 3 right now                */
    int           background;
    int           reported;         /* finished background job announced   */
    uint32_t      parent_id;
    uint32_t      brk;              /* heap break                         */
    uint32_t      upages;           /* page budget, replaces mapped[]      */
    vfs_node_t   *ufds[PROC_UFD_MAX];
    char          args[PROC_ARGS_MAX];
    uint32_t      fault_exc, fault_err, fault_eip, fault_cr2;
    wait_queue_t  waiters;          /* the parent blocks here              */
    struct task  *task;             /* NULL once the task has exited       */
} proc_t;
```

`mapped[MAX_UPAGES]` and `is_mapped()` are deleted. Their only jobs were
deduplication and a cap, and `paging_is_mapped(va)` answers the first question
against the address space that is actually loaded, which is more correct than a
side table. A `upages` counter keeps the cap. That also removes a 4 KB static
and an O(n) scan per mapped page.

`proc_t` outliving its task matters: `reap_one_dead` frees the `task_t` and its
stack, so an exit code stored on the task would vanish before the parent read
it. The slot survives as a zombie until collected.

## Lifecycle

- **Spawn.** `proc_spawn(path, args, background, &pid)` takes a slot, spawns a
  task on `proc_entry`, and returns. `proc_entry` runs the loader, then calls
  `proc_exit`.
- **Exit.** `proc_exit` records the code, sets `ZOMBIE`, clears `task`, and
  wakes `waiters`.
- **Foreground.** `cmd_cook` spawns, marks the process foreground, then calls
  `proc_wait(pid)`, which blocks on `waiters` and returns the exit code,
  freeing the slot.
- **Background.** Nobody waits. The slot stays a zombie, and the shell reports
  it at the next prompt like a real shell (`[3] done  echo.elf (exit 0)`) and
  frees it then.

## Kill and Ctrl-C

Killing a ring-3 process by setting `TASK_DEAD` would leak its whole address
space: `exec_elf` never returns, so `leave_proc_space` never runs. Instead the
victim tears itself down.

`proc_kill(pid)` sets `killed = 1`. The flag is noticed, and the unwind taken,
only at points where the kernel holds no locks:

1. The top of `syscall_dispatch`, before any lock is taken.
2. An IRQ that interrupted ring-3 code, detected with `(regs->cs & 3) == 3` in
   `irq_handler`, after the EOI.

Both unwind through `user_mode_exit` into the victim's own `exec_elf`, which
already closes fds and frees the address space. This is the path the existing
ring-3 fault handler takes, so it is proven.

A timer IRQ that interrupted the *kernel* half of a syscall must not unwind:
the victim could be holding the FAT mutex, and abandoning the frame would
deadlock the filesystem permanently. Hence the `cs & 3` test rather than a
check on `in_user` alone, which stays set across a syscall.

Ctrl-C is caught in the keyboard layer, not the shell, because a foreground
parent is blocked in `proc_wait` and is not reading keys. When a foreground
process exists, `0x03` marks it killed and is swallowed; with no foreground
process it reaches the shell unchanged and still cancels the input line.

A foreground program blocked reading stdin has to notice too, so the `SYS_READ`
stdin path polls `keyboard_available()` with yields and checks `killed` each
turn rather than calling the uninterruptible `keyboard_getchar()`.

A process blocked on a mutex wait queue is left alone; its kill takes effect
when it wakes. Forcing it `READY` while it is still linked on the queue would
corrupt the queue.

## Console and stdin ownership

One foreground process at a time, recorded in `proc.c` and set by the shell
around a foreground run. `SYS_READ` on stdin from any other process returns 0,
which user code reads as EOF. VGA writes from a background process interleave
with the prompt, which is what a real terminal does; `vga.c` already serialises
each write against preemption, so output is garbled in order but never in
content.

## Shell surface

- `cook prog [args]` unchanged in appearance, now spawn plus wait.
- `cook prog &` prints `[pid] prog` and returns to the prompt.
- `jobs` lists live and finished processes.
- `kill <id>` prefers `proc_kill` for a pid that is a process, falls back to
  `task_kill`.
- `ps` gains a `PROC` marker column so a process is distinguishable from a
  plain kernel task.
- Doom's `task_count() > 1` guard becomes "count tasks that are not DEAD", so a
  just-exited program does not make `doom` refuse until the reaper catches up.

## Out of scope, deliberately

`fork`, `exec` replacing an image, pipes and redirection (M3), `fg`/`bg` and
process groups (M2), threads inside a process, and any change to soupyc.
`CHALLENGE=1` keeps building and keeps its unvalidated `p_offset` path.

## Testing

Added to `scripts/smoke-test.sh`, which is the project's gate:

1. **Two concurrent processes.** A new `user/marker.c` prints `[p:<arg>]`
   markers with yields between them. Two backgrounded copies with different
   args must interleave in the serial log.
2. **Per-task esp0.** Each process klogs its `esp0` on entry to ring 3. The
   two values must differ, which is the invariant stated directly.
3. **Kill a ring-3 spinner.** A new `user/hog.c` loops forever making no
   syscalls, so only the IRQ unwind path can kill it. `kill <pid>` must end it
   and the shell must stay alive.
4. **Kill a syscall looper.** `spin.elf` already yields in a loop, covering the
   `syscall_dispatch` unwind path.
5. **Exit status.** A foreground `cook` reports the program's code; a
   background one is announced at the next prompt.
6. **No address-space leak.** `pmm_free_pages()` before and after running a
   program twenty times must match.

## What is most likely to bite

- Getting `esp0` wrong by one frame is silent until the second process traps.
  Test 2 asserts the value rather than inferring it from behaviour.
- The unwind abandons a stack frame. Any new lock taken before the
  `syscall_dispatch` kill check would leak on a kill.
- `proc_t` slots are a fixed 8. Exhaustion must be a clean refusal, not a
  panic.

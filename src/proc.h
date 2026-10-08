#pragma once
#include <stdint.h>
#include "task.h"
#include "vfs.h"

/* Ring-3 processes.
 *
 * A process IS a kernel task: proc_spawn() creates a task whose entry loads an
 * ELF and runs it in ring 3, and the pid is that task's id, so `ps` keeps
 * showing one kind of thing. What distinguishes a process from a plain kernel
 * task is this struct, which holds the state that used to be file-scope in
 * usermode.c and therefore limited the machine to one program at a time.
 *
 * The slot outlives the task on purpose. task.c's reaper frees a DEAD task_t
 * and its stack, so an exit code kept on the task would vanish before the
 * parent could read it; the slot stays a zombie until proc_wait() collects it
 * or proc_report_finished() announces it. */

#define PROC_MAX        8      /* concurrent processes (slots, no malloc)   */
#define PROC_UFD_MAX    16     /* open files per process, fd >= 3           */
#define PROC_ARGS_MAX   128    /* command line handed to the program        */
#define PROC_NAME_MAX   64     /* program path, for ps/jobs                 */
#define PROC_PAGE_CAP   1024   /* user pages per process (4 MB)             */

/* Exit code for a process killed from outside. Mirrors the fault convention
 * in usermode.c (-(128 + signal/exception), SIGKILL being 9). */
#define PROC_EXIT_KILLED  (-(128 + 9))

typedef enum {
    PROC_FREE    = 0,     /* slot unused                                   */
    PROC_RUNNING = 1,     /* task alive (loading, in ring 3, or in syscall) */
    PROC_STOPPED = 2,     /* parked at a safe point; alive, costs only RAM  */
    PROC_ZOMBIE  = 3,     /* exited; exit_code valid, waiting to be reaped  */
} proc_state_t;

typedef struct proc {
    uint32_t      pid;                 /* == owning task id                */
    char          name[PROC_NAME_MAX];
    proc_state_t  state;
    int           exit_code;
    volatile int  killed;              /* pending kill, taken at a safe point */
    volatile int  stop_pending;        /* Ctrl-Z, parked at a safe point     */
    volatile int  in_user;             /* executing ring 3 right now        */
    int           background;
    int           reported;            /* finished bg job already announced */
    uint32_t      parent_id;           /* task id that spawned us           */
    uint32_t      brk;                 /* user heap break (sbrk)            */
    uint32_t      upages;              /* user pages mapped, vs PROC_PAGE_CAP */
    uint32_t      demand_pages;        /* heap pages faulted in on first touch */
    uint32_t      start_tick;          /* when it was spawned, for the exit line */
    uint16_t     *swap_slot;           /* heap page -> swap slot + 1, or 0 (swap.c) */
    uint16_t     *fifo;                /* resident heap pages, oldest first    */
    uint32_t      fifo_head, fifo_len;
    uint32_t      swaps_out, swaps_in;
    uint32_t      esp0;                /* ring-0 stack while in ring 3      */
    vfs_node_t   *ufds[PROC_UFD_MAX];
    char          args[PROC_ARGS_MAX];
    wait_queue_t  waiters;             /* the parent blocks here            */
    wait_queue_t  cont_wq;             /* the process itself parks here      */
    struct task  *task;                /* NULL once the process has exited  */
    void         *term;                /* the terminal it was cooked on (v0.36.0) */
} proc_t;

/* The process the calling task is running, or NULL for a plain kernel task. */
proc_t  *proc_current(void);

/* Load `path` and run it in ring 3 on a task of its own. Returns 0 and sets
 * *out_pid on success, -1 if every slot is in use, -2 if the task or its stack
 * could not be allocated. Does not wait: see proc_wait.
 *
 * `in` and `out` bind the process's stdin and stdout before it starts; NULL
 * leaves that descriptor on the console default. The process owns whatever is
 * passed and closes it when it exits, which is what signals EOF down a pipe,
 * so the caller must not close them or hand the same handle to two
 * processes. */
int      proc_spawn(const char *path, const char *args, int background,
                    vfs_node_t *in, vfs_node_t *out, uint32_t *out_pid);

/* Block until `pid` exits or stops.
 *   1  it exited: *code holds the exit code and the slot has been collected
 *   0  it stopped: the slot is kept, the job still exists (see proc_continue)
 *  -1  no such process
 * A stopped child wakes its parent exactly like an exited one, which is how
 * Ctrl-Z returns the shell to a prompt. */
int      proc_wait(uint32_t pid, int *code);

/* Flag `pid` to be killed. The process tears itself down at its next safe
 * point (syscall entry, or a timer IRQ that interrupted ring-3 code), because
 * marking its task DEAD from here would leak its whole address space: exec
 * would never return and never free it. Returns 0 if flagged, -1 if there is
 * no such live process, or if it is the caller. A process blocked on a wait
 * queue is left linked there and notices when it wakes. */
int      proc_kill(uint32_t pid);

/* Flag a process directly, with no lookup and no logging. Interrupt-safe;
 * these are the Ctrl-C and Ctrl-Z paths. */
void     proc_flag_kill(proc_t *p);
void     proc_flag_stop(proc_t *p);

/* Called by a process on itself, at a safe point, when a stop is pending: it
 * parks until continued or killed. Enables interrupts first, because both safe
 * points are interrupt gates and parking ends in hlt. */
void     proc_take_stop(void);

/* Resume a stopped process. No-op on anything else. */
void     proc_continue(proc_t *p);

/* Called by the process's own task when the program is done. */
void     proc_exit(proc_t *p, int exit_code);

proc_t  *proc_by_pid(uint32_t pid);

/* Most recently spawned live process, or NULL. Backs the shell's `kill %`. */
proc_t  *proc_most_recent(void);
/* The same, among the processes cooked on terminal `term` only, so `kill %`
 * in one shell never reaches another shell's job. */
proc_t  *proc_most_recent_on(void *term);
/* A terminal hung up: kill every live process cooked on it. */
void     proc_kill_by_term(void *term);

/* Iterate every non-free slot: start with *idx = 0, stop when NULL. */
proc_t  *proc_next(int *idx);

uint32_t proc_count_live(void);

/* One-letter state for ps/jobs: R=running, Z=zombie. */
char     proc_state_letter(proc_state_t s);

/* The process that owns the keyboard. Set around a foreground run by the
 * shell; stdin reads from any other process see EOF, and Ctrl-C in the
 * keyboard IRQ kills this one. NULL means the shell owns input. */
void     proc_set_foreground(proc_t *p);
proc_t  *proc_foreground(void);

/* Non-zero if the calling process has a kill pending. */
int      proc_kill_pending(void);

/* Announce and collect finished background jobs. Calls report() for each,
 * frees their slots, and returns how many were reported. Called by the shell
 * just before it draws a prompt, the way a real shell does it. */
int      proc_report_finished(void (*report)(const proc_t *p));
/* The same, for the jobs cooked on one terminal only. */
int      proc_report_finished_on(void *term, void (*report)(const proc_t *p));

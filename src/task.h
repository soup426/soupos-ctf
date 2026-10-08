#pragma once
#include <stdint.h>

/* Preemptive + cooperative task scheduler - ring 0.
 * Tasks switch when they call task_yield() (or a blocking call that yields)
 * AND, once preemption is enabled, when the 100 Hz timer IRQ preempts them -
 * so a task that never yields can no longer monopolise the CPU. */

struct proc;   /* ring-3 process (proc.h); tasks only hold a pointer */

typedef enum {
    TASK_READY   = 0,    /* runnable, waiting for CPU */
    TASK_RUNNING = 1,    /* currently executing */
    TASK_BLOCKED = 2,    /* waiting on event (kb, timer, I/O) */
    TASK_DEAD    = 3,    /* exited, stack pending free */
} task_state_t;

typedef struct task {
    uint32_t      esp;           /* saved stack pointer while suspended  */
    uint32_t      stack_base;    /* heap block holding the task's stack  */
    uint32_t      stack_size;
    uint32_t      id;
    char          name[16];
    task_state_t  state;
    struct task  *next;          /* intrusive circular ready list        */
    struct task  *wq_next;       /* linkage on a wait_queue_t (or NULL)  */
    struct wait_queue *blocked_on; /* the queue we are parked on, or NULL */
    uint32_t      wake_tick;     /* for task_sleep: PIT tick to wake at  */
    void        (*entry)(void *);/* first-run entry point (via trampoline) */
    void         *arg;           /* argument passed to entry             */
    int           preempt_depth; /* >0 = this task must not be preempted */
    uint32_t     *page_dir;      /* address space; kernel's unless in exec */
    /* Ring-0 esp to resume at when this task leaves ring 3, and the esp0 the
     * CPU must load if it traps *from* ring 3. The same word serves both: it
     * is the kernel esp captured by user_mode_enter after its own pushes, so a
     * trap frame pushed there lands below the frame user_mode_enter left on
     * this stack instead of on top of it. Zero when the task has never entered
     * ring 3. task_yield keeps tss.esp0 in step with it. */
    uint32_t      user_resume_esp;
    struct proc  *proc;          /* ring-3 process this task runs, or NULL  */
    struct term  *term;          /* where it prints and reads; NULL = the console.
                                  * Inherited by every task it spawns (v0.36.0) */
    void         *shell;         /* the shell it belongs to (shell_t), inherited */
    void         *soupyc;        /* soupyc_ctx_t while this task interprets  */
    int           service;       /* a background service (fbcon, mixer): it
                                  * is alive, but it is not another program
                                  * competing for the machine */
    uint32_t      cpu_window;    /* timer ticks held in the second so far   */
    uint32_t      cpu_last;      /* ... and in the second just finished:
                                  * the window is 100 ticks, so this IS a
                                  * percentage, no division needed          */
    int           sys_write;     /* inside a kernel-internal write (the roster,
                                  * the login record, a home...): may use the
                                  * disk's reserve whoever the task's cook is
                                  * (v0.55.5) */
} task_t;

/* Simple intrusive wait queue - FIFO singly-linked list of blocked tasks.
 * Zero-init or set .head = 0 before first use. */
typedef struct wait_queue {
    struct task *head;
} wait_queue_t;

/* Converts the current kernel thread into task 0 ("kernel").
 * Must be called once after heap_init(), before any other task API. */
void     task_init(void);

/* Create a new ready task. Returns NULL on OOM.
 * entry(arg) runs in its own 16 KB stack. If entry returns, the task
 * is marked DEAD and scheduled away forever. */
task_t  *task_spawn(const char *name, void (*entry)(void *), void *arg);

/* Voluntarily hand off to the next ready task. No-op if none. */
void     task_yield(void);

/* Preemption control. task_preempt_enable() arms timer-driven preemption
 * (call once after task_init + at least one spawn). sched_preempt() is the
 * hook the timer IRQ calls each tick - it switches tasks only when it is safe
 * (preemption armed, not inside the scheduler, more than one task). The
 * preempt_disable/enable pair brackets a region that must not be switched
 * away by the timer (cooperative task_yield still works); they nest. */
void     task_preempt_enable(void);
void     sched_preempt(void);
void     preempt_disable(void);
void     preempt_enable(void);

/* Mark current task DEAD and schedule away forever. Normally you don't
 * call this directly - returning from a task's entry function arrives
 * here via the initial stack frame. */
void     task_exit(void) __attribute__((noreturn));

task_t  *task_current(void);
uint32_t task_count(void);

/* Like task_count(), but ignores DEAD tasks awaiting the reaper. Use this to
 * ask "is any other task actually running?". */
uint32_t task_count_alive(void);

/* Alive tasks that are not background services. This is the number a program
 * asking "do I have the machine to myself?" actually wants. */
uint32_t task_count_users(void);

/* Mark the calling task a background service. */
void     task_set_service(void);

/* Charge this timer tick to whichever task is running, and roll the window
 * once a second. Called from the timer interrupt and nowhere else.
 *
 * It measures WHO HELD THE CPU, which is not the same as who did work: a task
 * halted in a wait loop is still the current task, so the shell sitting at a
 * prompt reads as busy. What the number is good for is the opposite question -
 * whether a background service is costing more than it should. */
void     task_cpu_tick(void);

/* Iteration. Start at task_list_head() (NULL if no tasks), follow .next
 * until it wraps back to the head. The list is circular so do not
 * dereference past the head a second time. */
task_t  *task_list_head(void);

/* Lookup by id. Returns NULL if not found. */
task_t  *task_by_id(uint32_t id);

/* Mark a task DEAD. Refuses to kill task 0 (kernel) or the current
 * task. Returns 0 on success, -1 on invalid/refused. */
int      task_kill(uint32_t id);

/* One-letter status for ps: R=running, r=ready, B=blocked, D=dead */
char     task_state_letter(task_state_t s);

/* Sleep the current task for at least `ms` milliseconds. Uses the PIT
 * (100 Hz, 10 ms per tick); sub-10ms sleeps are rounded up to one tick.
 * Safe to call before task_init() - falls back to busy-hlt. */
void     task_sleep(uint32_t ms);

/* Halt until the next interrupt, and have the timer charge that time to IDLE
 * rather than to whoever was waiting. Every wait loop in the kernel ends in
 * this; a bare `hlt` makes the shell "use" 90% of the cpu at a prompt. */
void     cpu_halt(void);
uint32_t task_idle_last(void);     /* idle ticks in the last second: a percentage */

/* Wait-queue primitives. task_block_on marks the current task BLOCKED,
 * links it onto wq, and yields. task_wake / task_wake_one move tasks
 * back to READY. */
/* ── Mutex ────────────────────────────────────────────────────────────────
 * A sleeping, recursive lock for driver state.
 *
 * preempt_disable is the wrong tool for the drivers: it stops the timer
 * switching tasks, but a task that yields voluntarily inside the region (and
 * FAT does, via the ATA PIO wait loop) still lets another task walk straight
 * into the same critical section. A mutex blocks the second task instead.
 *
 * Recursive because the FAT entry points call each other; taking the lock at
 * each public entry would otherwise self-deadlock.
 *
 * Safe before the scheduler exists: with no current task there is no
 * concurrency, and lock/unlock become no-ops. */
typedef struct {
    struct task  *owner;
    int           depth;
    wait_queue_t  wq;
} mutex_t;

void     mutex_lock(mutex_t *m);
void     mutex_unlock(mutex_t *m);

void     task_block_on(wait_queue_t *wq);
void     task_wake(wait_queue_t *wq);
void     task_wake_one(wait_queue_t *wq);
/* Pull one task off whatever queue it is blocked on and make it ready. For a
 * kill: a process parked in a pipe read never reaches the point that honours
 * the flag unless something wakes it. Every block site loops and re-checks
 * its condition, so an early wake is always safe. */
void     task_unblock(task_t *t);

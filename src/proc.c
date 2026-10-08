/* proc.c - the ring-3 process table.
 *
 * A process is a kernel task plus a proc_t. proc_spawn gives a program its own
 * task; that task's entry (proc_entry) loads the ELF and enters ring 3, and
 * when the program is done, faults, or is killed, the loader returns and
 * proc_exit records the outcome. Nothing here knows how to load an ELF: that
 * is usermode.c's job, reached through usermode_run().
 *
 * Concurrency rules, all of them small:
 *   - Slot allocation and the task/slot linking happen with preemption
 *     disabled, so two shells cannot take the same slot and a spawned task
 *     cannot start before its proc pointer is in place.
 *   - proc_wait re-checks the child's state inside a preempt_disable region
 *     before blocking, the same lost-wakeup guard mutex_lock uses.
 *   - A slot is freed by whoever collects it (proc_wait, or
 *     proc_report_finished), never by the process itself.
 */
#include "proc.h"
#include "users.h"
#include "timer.h"
#include "swap.h"
#include "usermode.h"
#include "task.h"
#include "str.h"
#include "klog.h"
#include "pmm.h"

static proc_t   procs[PROC_MAX];
static proc_t  *foreground;

proc_t *proc_current(void) {
    task_t *t = task_current();
    return t ? t->proc : 0;
}

proc_t *proc_by_pid(uint32_t pid) {
    for (int i = 0; i < PROC_MAX; i++)
        if (procs[i].state != PROC_FREE && procs[i].pid == pid)
            return &procs[i];
    return 0;
}

proc_t *proc_next(int *idx) {
    if (!idx) return 0;
    while (*idx < PROC_MAX) {
        proc_t *p = &procs[(*idx)++];
        if (p->state != PROC_FREE) return p;
    }
    return 0;
}

uint32_t proc_count_live(void) {
    uint32_t n = 0;
    for (int i = 0; i < PROC_MAX; i++)
        if (procs[i].state == PROC_RUNNING || procs[i].state == PROC_STOPPED) n++;
    return n;
}

char proc_state_letter(proc_state_t s) {
    switch (s) {
        case PROC_RUNNING: return 'R';
        case PROC_STOPPED: return 'T';
        case PROC_ZOMBIE:  return 'Z';
        case PROC_FREE:    return '-';
    }
    return '?';
}

void proc_set_foreground(proc_t *p) { foreground = p; }
proc_t *proc_foreground(void)       { return foreground; }

int proc_kill_pending(void) {
    proc_t *p = proc_current();
    return p && p->killed;
}

/* The last path component, so `ps` (task names are 16 bytes) shows something
 * recognisable rather than the start of a directory. */
static const char *basename_of(const char *path) {
    const char *base = path;
    for (const char *s = path; *s; s++)
        if (*s == '/') base = s + 1;
    return base;
}

/* Entry point of every process task. The proc pointer arrives as the task
 * argument rather than through task_current()->proc so this cannot race with
 * proc_spawn's linking, and is installed on the task here as well so the
 * syscall layer can find it via proc_current(). */
static void proc_entry(void *arg) {
    proc_t *p = (proc_t *)arg;
    task_t *me = task_current();
    if (me) me->proc = p;

    int rc = usermode_run(p);
    proc_exit(p, rc);
}

int proc_spawn(const char *path, const char *args, const char *env, int background,
               vfs_node_t *in, vfs_node_t *out, vfs_node_t *err, int err_as_out,
               uint32_t *out_pid) {
    if (!path || !*path) return -1;

    preempt_disable();      /* slot claim + linking must be indivisible */

    proc_t *p = 0;
    uint8_t me = users_current_uid();
    int mine = 0, cooks = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        if (procs[i].state == PROC_FREE) { if (!p) p = &procs[i]; continue; }
        if (procs[i].owner == me) mine++;
        if (procs[i].owner != 0)  cooks++;
    }
    if (!p) { preempt_enable(); return PROC_ERR_FULL; }
    if (me != 0) {                         /* the headchef is not limited */
        if (mine  >= PROC_PER_COOK)            { preempt_enable(); return PROC_ERR_COOK_CAP; }
        if (cooks >= PROC_MAX - PROC_RESERVE)  { preempt_enable(); return PROC_ERR_RESERVED; }
    }

    memset(p, 0, sizeof(*p));
    p->state     = PROC_RUNNING;
    p->start_tick = timer_get_ticks();     /* claims the slot                    */
    p->parent_id = task_current() ? task_current()->id : 0;
    p->background = background;
    p->owner     = users_current_uid();    /* the spawning shell's cook */
    strncpy(p->name, path, PROC_NAME_MAX - 1);
    p->name[PROC_NAME_MAX - 1] = '\0';
    if (args) {
        strncpy(p->args, args, PROC_ARGS_MAX - 1);
        p->args[PROC_ARGS_MAX - 1] = '\0';
    }
    if (env) {
        strncpy(p->env, env, PROC_ENV_MAX - 1);
        p->env[PROC_ENV_MAX - 1] = '\0';
    }

    /* Inherited stdio, bound before the task can run. NULL leaves the
     * descriptor unbound, which the syscall layer reads as "use the console". */
    p->ufds[0] = in;
    p->ufds[1] = out;
    p->ufds[2] = err;
    p->err_as_out = (uint8_t)err_as_out;

    task_t *t = task_spawn(basename_of(p->name), proc_entry, p);
    if (!t) {
        p->state = PROC_FREE;
        preempt_enable();
        return -2;
    }
    t->proc = p;
    p->task = t;
    p->term = t->term;               /* inherited from the cooking task */
    p->pid  = t->id;
    if (out_pid) *out_pid = p->pid;

    preempt_enable();
    /* Arguments are in the line so a log reader can tell two copies of the
     * same program apart, which is what the concurrency test needs. */
    klog("[proc %u] spawned %s %s%s\n", p->pid, p->name, p->args,
         background ? " &" : "");
    return 0;
}

void proc_exit(proc_t *p, int exit_code) {
    if (!p) return;

    preempt_disable();
    p->exit_code = exit_code;
    p->in_user   = 0;
    p->state     = PROC_ZOMBIE;
    /* The task is about to mark itself DEAD and be reaped; drop the link so
     * nothing follows it to freed memory. */
    if (p->task) p->task->proc = 0;
    p->task = 0;
    if (foreground == p) foreground = 0;

    /* Log BEFORE waking the parent, and inside the region. Once the parent
     * runs it may collect this slot, and the next spawn may reuse and memset
     * it, so reading p->pid/p->name afterwards could describe a different
     * process entirely.
     *
     * The free page count is in the line on purpose: the loader has already
     * torn the address space down by now, so two runs of one program must
     * report the same number. That is the leak test. */
    klog("[proc %u] %s exited with code %d, %u pages free, %u ticks\n",
         p->pid, p->name, exit_code, pmm_free_pages(), timer_get_ticks() - p->start_tick);
    if (p->demand_pages)
        klog("[demand] %s faulted in %u pages on first touch\n", p->name, p->demand_pages);
    if (p->stack_pages)
        klog("[stack] %s grew its stack by %u pages\n", p->name, p->stack_pages);
    p->stack_pages = 0;
    if (p->swaps_out || p->swaps_in)
        klog("[swap] %s: %u pages out, %u back in\n", p->name, p->swaps_out, p->swaps_in);
    swap_release(p);
    p->swaps_out = p->swaps_in = 0;

    task_wake(&p->waiters);          /* the parent, if it is waiting */
    preempt_enable();
}

int proc_wait(uint32_t pid, int *code) {
    proc_t *p = proc_by_pid(pid);
    if (!p) return -1;

    /* Re-check inside the no-preemption region before blocking: without it the
     * child could exit between the test and the enqueue, wake an empty queue,
     * and leave us blocked forever. task_block_on links us on before it
     * yields, and a voluntary yield still works with preemption disabled. */
    for (;;) {
        preempt_disable();
        if (p->state != PROC_RUNNING) { preempt_enable(); break; }
        task_block_on(&p->waiters);
        preempt_enable();
    }

    /* Stopped, not finished: the job lives on, so the slot stays. */
    if (p->state == PROC_STOPPED) return 0;

    if (code) *code = p->exit_code;
    if (foreground == p) foreground = 0;
    p->state = PROC_FREE;            /* collected */
    return 1;
}

/* Just set the flag. Safe from interrupt context: one volatile store, no
 * logging, no allocation, no lock. This is what the keyboard IRQ calls for
 * Ctrl-C, where the interrupted task is very often the target itself. */
void proc_flag_kill(proc_t *p) {
    if (p && (p->state == PROC_RUNNING || p->state == PROC_STOPPED))
        p->killed = 1;
}

void proc_flag_stop(proc_t *p) {
    if (p && p->state == PROC_RUNNING) p->stop_pending = 1;
}

/* Park the calling process until someone continues it (or kills it).
 *
 * Only ever called from a safe point, so no kernel lock is held and parking
 * cannot strand one. Interrupts are enabled first: both safe points are
 * reached through interrupt gates, so IF is clear, and task_block_on ends in
 * hlt when nothing else is runnable - parking with interrupts off would never
 * wake. SYS_YIELD sets the same precedent. */
void proc_take_stop(void) {
    proc_t *p = proc_current();
    if (!p) return;

    p->stop_pending = 0;
    p->state        = PROC_STOPPED;
    /* Hand the terminal back and wake whoever is waiting on us, so the shell
     * returns to a prompt instead of blocking on a process that is not going
     * to finish. */
    if (proc_foreground() == p) proc_set_foreground(0);
    task_wake(&p->waiters);
    klog("[proc %u] %s stopped\n", p->pid, p->name);

    /* Park with interrupts on, then put IF back the way the caller had it.
     * Both safe points are interrupt gates, so the caller's IF is clear, and
     * the IRQ handler in particular must not continue with interrupts on: it
     * goes on to call sched_preempt, which expects to be the only thing
     * entering the scheduler. */
    uint32_t caller_flags;
    __asm__ volatile ("pushf; pop %0" : "=r"(caller_flags));
    __asm__ volatile ("sti");

    /* Same lost-wakeup guard as proc_wait: re-check inside the region, and let
     * a pending kill out of the loop so the safe point we return to can take
     * it. */
    for (;;) {
        preempt_disable();
        if (p->state != PROC_STOPPED || p->killed) { preempt_enable(); break; }
        task_block_on(&p->cont_wq);
        preempt_enable();
    }
    p->state = PROC_RUNNING;
    if (!(caller_flags & 0x200)) __asm__ volatile ("cli");
}

void proc_continue(proc_t *p) {
    if (!p || p->state != PROC_STOPPED) return;
    preempt_disable();
    p->state = PROC_RUNNING;
    task_wake(&p->cont_wq);
    preempt_enable();
    klog("[proc %u] %s continued\n", p->pid, p->name);
}

int proc_kill(uint32_t pid) {
    proc_t *p = proc_by_pid(pid);
    if (!p || (p->state != PROC_RUNNING && p->state != PROC_STOPPED)) return -1;
    /* Flagging the caller's own process is deliberately allowed: the flag is
     * acted on at the next safe point, which for a self-kill is the return
     * from this very trap. The keyboard IRQ depends on that, since the task it
     * interrupted is usually the program being Ctrl-C'd. */
    proc_flag_kill(p);
    /* A stopped process is parked and would never reach a safe point to
     * notice. Wake it so it can die. */
    if (p->state == PROC_STOPPED) {
        preempt_disable();
        p->state = PROC_RUNNING;
        task_wake(&p->cont_wq);
        preempt_enable();
    } else if (p->task) {
        /* A running process blocked in the kernel - a pipe read with nothing
         * to read, say - is just as parked, and would stay there as long as
         * its peer did. The gate's `kill %` on a pipeline hit this: the flag
         * went to the reader, the reader never woke, and the hog on the other
         * end kept 90% of the machine for the rest of the run. */
        preempt_disable();
        task_unblock(p->task);
        preempt_enable();
    }
    klog("[proc %u] kill flagged\n", pid);
    return 0;
}

/* The newest job: what a bare `fg`, `bg` and `kill %` target. Stopped counts
 * as live, since resuming one is the whole point of job control. Highest pid
 * wins, because task ids only ever increase. */
proc_t *proc_most_recent_on(void *term) {
    proc_t *best = 0;
    for (int i = 0; i < PROC_MAX; i++)
        if ((procs[i].state == PROC_RUNNING || procs[i].state == PROC_STOPPED) &&
            procs[i].term == term && (!best || procs[i].pid > best->pid))
            best = &procs[i];
    return best;
}

void proc_kill_by_term(void *term) {
    for (int i = 0; i < PROC_MAX; i++)
        if ((procs[i].state == PROC_RUNNING || procs[i].state == PROC_STOPPED) &&
            procs[i].term == term)
            proc_kill(procs[i].pid);
}

proc_t *proc_most_recent(void) {
    proc_t *best = 0;
    for (int i = 0; i < PROC_MAX; i++)
        if ((procs[i].state == PROC_RUNNING || procs[i].state == PROC_STOPPED) &&
            (!best || procs[i].pid > best->pid))
            best = &procs[i];
    return best;
}

int proc_report_finished_on(void *term, void (*report)(const proc_t *p)) {
    int n = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        proc_t *p = &procs[i];
        if (p->state != PROC_ZOMBIE || !p->background || p->reported || p->term != term) continue;
        p->reported = 1;
        if (report) report(p);
        p->state = PROC_FREE;
        n++;
    }
    return n;
}

int proc_report_finished(void (*report)(const proc_t *p)) {
    int n = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        proc_t *p = &procs[i];
        if (p->state != PROC_ZOMBIE || !p->background || p->reported) continue;
        p->reported = 1;
        if (report) report(p);
        p->state = PROC_FREE;
        n++;
    }
    return n;
}

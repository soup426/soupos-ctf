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
        if (procs[i].state == PROC_RUNNING) n++;
    return n;
}

char proc_state_letter(proc_state_t s) {
    switch (s) {
        case PROC_RUNNING: return 'R';
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

int proc_spawn(const char *path, const char *args, int background,
               uint32_t *out_pid) {
    if (!path || !*path) return -1;

    preempt_disable();      /* slot claim + linking must be indivisible */

    proc_t *p = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        if (procs[i].state == PROC_FREE) { p = &procs[i]; break; }
    }
    if (!p) { preempt_enable(); return -1; }

    memset(p, 0, sizeof(*p));
    p->state     = PROC_RUNNING;     /* claims the slot                    */
    p->parent_id = task_current() ? task_current()->id : 0;
    p->background = background;
    strncpy(p->name, path, PROC_NAME_MAX - 1);
    p->name[PROC_NAME_MAX - 1] = '\0';
    if (args) {
        strncpy(p->args, args, PROC_ARGS_MAX - 1);
        p->args[PROC_ARGS_MAX - 1] = '\0';
    }

    task_t *t = task_spawn(basename_of(p->name), proc_entry, p);
    if (!t) {
        p->state = PROC_FREE;
        preempt_enable();
        return -2;
    }
    t->proc = p;
    p->task = t;
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
    task_wake(&p->waiters);          /* the parent, if it is waiting */
    preempt_enable();

    /* Free page count goes in the line on purpose: the address space has
     * already been torn down by the loader, so two runs of the same program
     * must report the same number. That is the leak test. */
    klog("[proc %u] %s exited with code %d, %u pages free\n",
         p->pid, p->name, exit_code, pmm_free_pages());
}

int proc_wait(uint32_t pid) {
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

    int code = p->exit_code;
    if (foreground == p) foreground = 0;
    p->state = PROC_FREE;            /* collected */
    return code;
}

/* Just set the flag. Safe from interrupt context: one volatile store, no
 * logging, no allocation, no lock. This is what the keyboard IRQ calls for
 * Ctrl-C, where the interrupted task is very often the target itself. */
void proc_flag_kill(proc_t *p) {
    if (p && p->state == PROC_RUNNING) p->killed = 1;
}

int proc_kill(uint32_t pid) {
    proc_t *p = proc_by_pid(pid);
    if (!p || p->state != PROC_RUNNING) return -1;
    /* Flagging the caller's own process is deliberately allowed: the flag is
     * acted on at the next safe point, which for a self-kill is the return
     * from this very trap. The keyboard IRQ depends on that, since the task it
     * interrupted is usually the program being Ctrl-C'd. */
    proc_flag_kill(p);
    klog("[proc %u] kill flagged\n", pid);
    return 0;
}

/* The most recently spawned live process: what the shell's `kill %` targets.
 * Highest pid wins, because task ids only ever increase. */
proc_t *proc_most_recent(void) {
    proc_t *best = 0;
    for (int i = 0; i < PROC_MAX; i++)
        if (procs[i].state == PROC_RUNNING &&
            (!best || procs[i].pid > best->pid))
            best = &procs[i];
    return best;
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

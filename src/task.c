#include "task.h"
#include "heap.h"
#include "timer.h"
#include "paging.h"

#define DEFAULT_STACK_SIZE (16 * 1024)

extern void task_switch(uint32_t *old_esp_out, uint32_t new_esp);
static void task_start(void);   /* first-run trampoline (defined below) */

static task_t  *current   = 0;
static task_t  *list_head = 0;    /* any member of the circular list */
static uint32_t next_id   = 0;

/* Preemption state.
 *  - sched_locked > 0  : a scheduler op (or a preempt_disable region) is in
 *                        progress; the timer IRQ must NOT switch tasks, or it
 *                        would reenter the scheduler / a critical section.
 *  - preempt_armed     : timer-driven preemption has been turned on.
 * sched_locked balances across a context switch: whoever is switched *to*
 * runs the matching sched_unlock (either after its own task_switch, or via the
 * first-run trampoline), so a running task always sees sched_locked == 0. */
static volatile int sched_locked  = 0;
static volatile int preempt_armed = 0;

static inline void sched_lock(void)   { sched_locked++; }
static inline void sched_unlock(void) { sched_locked--; }

/* preempt_disable/enable are PER-TASK, and deliberately not sched_lock().
 *
 * sched_locked is the scheduler's own lock and is designed to unbalance
 * across a context switch: task_yield takes it, and whoever is switched *to*
 * runs the matching release. That is exactly right for handing off a switch,
 * and exactly wrong for a caller-held critical section. Routing
 * preempt_disable through it meant a region that yielded had its release run
 * by an unrelated task, leaving that task permanently unpreemptable while the
 * original ran on with no protection.
 *
 * Keeping the depth in task_t means it travels with the task that took it. */
void preempt_disable(void) {
    if (current) current->preempt_depth++;
}
void preempt_enable(void) {
    if (current && current->preempt_depth > 0) current->preempt_depth--;
}
void task_preempt_enable(void) { preempt_armed = 1; }

static void list_add(task_t *t) {
    if (!list_head) {
        list_head = t;
        t->next   = t;
    } else {
        t->next         = list_head->next;
        list_head->next = t;
    }
}

/* Wake any BLOCKED task whose task_sleep deadline has passed. O(n)
 * sweep - fine since we typically have < ~10 tasks. */
static void wake_expired_sleepers(void) {
    if (!list_head) return;
    uint32_t now = timer_get_ticks();
    task_t *t = list_head;
    do {
        if (t->state == TASK_BLOCKED && t->wake_tick != 0 &&
            (int32_t)(now - t->wake_tick) >= 0) {
            t->state     = TASK_READY;
            t->wake_tick = 0;
        }
        t = t->next;
    } while (t != list_head);
}

static task_t *pick_next(void) {
    if (!current) return 0;
    task_t *t = current->next;
    /* Walk once around looking for a READY peer. Bounded in case of
     * pathological self-loops during early bring-up. */
    for (uint32_t i = 0; i < 1024 && t != current; i++) {
        if (t->state == TASK_READY) return t;
        t = t->next;
    }
    /* Nobody else is runnable - stick with current if it still wants CPU. */
    if (current->state == TASK_RUNNING || current->state == TASK_READY)
        return current;
    return 0;
}

/* Unlink + free one non-current DEAD task per pass. Called from yield
 * so exited tasks don't leak their 16 KB stack indefinitely. */
static void reap_one_dead(void) {
    if (!current) return;
    task_t *prev = current;
    task_t *t    = current->next;
    int guard    = 1024;
    while (t != current && guard--) {
        task_t *next = t->next;
        if (t->state == TASK_DEAD) {
            prev->next = next;
            if (list_head == t) list_head = (next == t) ? 0 : next;
            if (t->stack_base) kfree((void *)t->stack_base);
            kfree(t);
            return;
        }
        prev = t;
        t    = next;
    }
}

void task_init(void) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    t->esp        = 0;      /* filled on first switch out of this task */
    t->stack_base = 0;      /* boot stack - never freed */
    t->stack_size = 0;
    t->id         = next_id++;
    t->state      = TASK_RUNNING;
    t->next       = t;
    t->wq_next    = 0;
    t->wake_tick  = 0;
    t->entry      = 0;
    t->arg        = 0;
    t->preempt_depth = 0;   /* kmalloc does not zero */
    t->page_dir      = paging_kernel_dir();

    const char nm[] = "kernel";
    for (uint32_t i = 0; i < sizeof(nm); i++) t->name[i] = nm[i];

    list_head = t;
    current   = t;
}

task_t *task_spawn(const char *name, void (*entry)(void *), void *arg) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) return 0;

    uint8_t *stack = (uint8_t *)kmalloc(DEFAULT_STACK_SIZE);
    if (!stack) { kfree(t); return 0; }

    t->id         = next_id++;
    t->state      = TASK_READY;
    t->stack_base = (uint32_t)stack;
    t->stack_size = DEFAULT_STACK_SIZE;
    t->wq_next    = 0;
    t->wake_tick  = 0;
    t->entry      = entry;
    t->arg        = arg;
    t->preempt_depth = 0;   /* kmalloc does not zero */
    t->page_dir      = paging_kernel_dir();

    uint32_t i = 0;
    while (name && name[i] && i < sizeof(t->name) - 1) {
        t->name[i] = name[i];
        i++;
    }
    t->name[i] = 0;

    /* Seed the stack so the first task_switch to us pops EFLAGS + the four
     * callee-saved regs and then `ret`s into task_start (the trampoline that
     * releases the scheduler lock and calls entry(arg)). Layout must match
     * task_switch.asm's pop order: ebp, edi, esi, ebx, popf, ret. */
    uint32_t *sp = (uint32_t *)(stack + DEFAULT_STACK_SIZE);
    *--sp = (uint32_t)task_start;   /* ret addr from task_switch */
    *--sp = 0x202;                  /* eflags: IF set (bit 9) + reserved bit 1 */
    *--sp = 0;                      /* ebx */
    *--sp = 0;                      /* esi */
    *--sp = 0;                      /* edi */
    *--sp = 0;                      /* ebp */
    t->esp = (uint32_t)sp;

    list_add(t);
    return t;
}

void task_yield(void) {
    if (!current) return;
    /* The whole pick+switch is a scheduler critical region: holding the lock
     * stops a timer preemption from reentering task_yield (and the reaper /
     * list walk) mid-flight. The lock is released by whichever task we switch
     * to (after its own task_switch, or via the trampoline on first run); the
     * matching sched_unlock below runs when *this* task is resumed later. */
    sched_lock();
    wake_expired_sleepers();   /* promote any timed-out sleepers to READY */
    reap_one_dead();
    task_t *prev = current;
    task_t *next = pick_next();
    if (!next || next == prev) { sched_unlock(); return; }

    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    current     = next;

    /* Follow the task into its address space. Safe to do before the stack
     * swap: kernel PDEs are shared by every directory, so the code and both
     * stacks stay mapped across the CR3 load. */
    if (next->page_dir && next->page_dir != prev->page_dir)
        paging_switch(next->page_dir);

    task_switch(&prev->esp, next->esp);
    sched_unlock();
}

/* First-run trampoline: a freshly spawned task is switched to with the
 * scheduler lock held (by whoever called task_yield); balance it here, then
 * run the real entry. If entry returns we fall through to task_exit. */
static void task_start(void) {
    sched_unlock();
    if (current && current->entry) current->entry(current->arg);
    task_exit();
}

/* Timer-IRQ preemption hook. Called from irq_handler after the IRQ0 EOI, with
 * interrupts disabled. Switches tasks only when it is safe to do so. */
void sched_preempt(void) {
    if (!preempt_armed || sched_locked) return;   /* not armed / in scheduler */
    if (!current) return;
    if (current->preempt_depth > 0) return;       /* task holds preempt_disable */
    /* task_yield walks/mutates the list and may task_switch; sched_locked is 0
     * here (checked above) and we run with IF=0, so starting a switch is safe.
     * A timer firing during the switch will see sched_locked != 0 and skip.
     * Only bother if there is another task (task_yield no-ops otherwise, but
     * this avoids the list walk + reaper on every tick when running solo). */
    if (current->next != current)
        task_yield();
}

void task_exit(void) {
    if (current) current->state = TASK_DEAD;
    for (;;) {
        task_yield();
        /* If nothing else is runnable, park until the next IRQ wakes
         * a blocked task. Without this we would spin burning CPU. */
        __asm__ volatile ("hlt");
    }
}

task_t *task_current(void) { return current; }

uint32_t task_count(void) {
    if (!list_head) return 0;
    uint32_t n = 0;
    task_t *t = list_head;
    do { n++; t = t->next; } while (t != list_head);
    return n;
}

task_t *task_list_head(void) { return list_head; }

task_t *task_by_id(uint32_t id) {
    if (!list_head) return 0;
    task_t *t = list_head;
    do {
        if (t->id == id) return t;
        t = t->next;
    } while (t != list_head);
    return 0;
}

int task_kill(uint32_t id) {
    task_t *t = task_by_id(id);
    if (!t) return -1;
    if (t == current) return -1;     /* can't kill self - use exit */
    if (t->id == 0)   return -1;     /* can't kill kernel task    */
    t->state = TASK_DEAD;
    return 0;
}

char task_state_letter(task_state_t s) {
    switch (s) {
        case TASK_RUNNING: return 'R';
        case TASK_READY:   return 'r';
        case TASK_BLOCKED: return 'B';
        case TASK_DEAD:    return 'D';
    }
    return '?';
}

/* ---- task_sleep -------------------------------------------------------
 * Design: we record a wake_tick deadline directly on the task_t and mark
 * the task BLOCKED.  wake_expired_sleepers() (called at the top of every
 * task_yield) sweeps the list and moves expired tasks back to READY - no
 * separate sleep-list struct needed.  Between yields we hlt so we don't
 * burn CPU while nothing else wants to run either.
 */
void task_sleep(uint32_t ms) {
    if (!current) {
        /* Before task_init(): fall back to a simple hlt loop. */
        uint32_t ticks = (ms + 9) / 10;
        uint32_t start = timer_get_ticks();
        while ((timer_get_ticks() - start) < ticks)
            __asm__ volatile ("hlt");
        return;
    }

    /* Convert ms -> ticks (100 Hz -> 10 ms/tick).  Round up so that
     * task_sleep(1) waits at least one tick, not zero. */
    uint32_t ticks = (ms + 9) / 10;
    if (ticks == 0) ticks = 1;

    current->wake_tick = timer_get_ticks() + ticks;
    current->state     = TASK_BLOCKED;

    /* Yield until we are woken.  wake_expired_sleepers() in task_yield
     * will flip us back to READY once the deadline passes. */
    while (current->state == TASK_BLOCKED) {
        task_yield();
        /* If we're still blocked after yielding, hlt until the next
         * timer IRQ fires (same pattern as keyboard_getchar). */
        if (current->state == TASK_BLOCKED)
            __asm__ volatile ("hlt");
    }
}

/* ---- wait-queue primitives ------------------------------------------- */

/* Mark current task BLOCKED, append it to wq, then yield.
 * Returns once task_wake / task_wake_one moves us back to READY. */
/* ── Mutex ─────────────────────────────────────────────────────────────── */

void mutex_lock(mutex_t *m) {
    task_t *me = task_current();
    if (!m || !me) return;              /* pre-scheduler: nothing to race */

    for (;;) {
        preempt_disable();              /* make test-and-take atomic vs the timer */
        if (!m->owner || m->owner == me) {
            m->owner = me;
            m->depth++;
            preempt_enable();
            return;
        }
        /* Held by someone else. Stay unpreemptable across the enqueue so the
         * owner cannot release and wake an empty queue between our check and
         * our going to sleep - the classic lost wakeup. task_block_on links
         * us on before it yields, and task_yield is voluntary so it works
         * while preemption is disabled. */
        task_block_on(&m->wq);
        preempt_enable();               /* woken: retry the loop */
    }
}

void mutex_unlock(mutex_t *m) {
    task_t *me = task_current();
    if (!m || !me) return;
    if (m->owner != me) return;         /* not ours: ignore */

    preempt_disable();
    if (--m->depth <= 0) {
        m->depth = 0;
        m->owner = 0;
        task_wake_one(&m->wq);
    }
    preempt_enable();
}

void task_block_on(wait_queue_t *wq) {
    if (!current || !wq) return;

    /* Append to tail so wake-order is FIFO. */
    current->wq_next = 0;
    if (!wq->head) {
        wq->head = current;
    } else {
        task_t *tail = wq->head;
        while (tail->wq_next) tail = tail->wq_next;
        tail->wq_next = current;
    }

    current->state = TASK_BLOCKED;

    /* Yield until someone calls task_wake / task_wake_one for us. */
    while (current->state == TASK_BLOCKED) {
        task_yield();
        if (current->state == TASK_BLOCKED)
            __asm__ volatile ("hlt");
    }
}

/* Wake ALL tasks on the queue. */
void task_wake(wait_queue_t *wq) {
    if (!wq) return;
    task_t *t = wq->head;
    while (t) {
        task_t *next = t->wq_next;
        t->wq_next = 0;
        if (t->state == TASK_BLOCKED) t->state = TASK_READY;
        t = next;
    }
    wq->head = 0;
}

/* Wake only the first (oldest) task on the queue. */
void task_wake_one(wait_queue_t *wq) {
    if (!wq || !wq->head) return;
    task_t *t  = wq->head;
    wq->head   = t->wq_next;
    t->wq_next = 0;
    if (t->state == TASK_BLOCKED) t->state = TASK_READY;
}

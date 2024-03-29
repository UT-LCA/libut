/*
 * sched.c - a scheduler for user-level threads
 */

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <base/stddef.h>
#include <base/lock.h>
#include <base/list.h>
#include <base/hash.h>
#include <base/limits.h>
#include <base/tcache.h>
#include <base/slab.h>
#include <base/log.h>
#include <runtime/sync.h>
#include <runtime/thread.h>

#include "defs.h"

/* the current running thread, or NULL if there isn't one */
__thread thread_t *__self;
/* a pointer to the top of the per-kthread (TLS) runtime stack */
static __thread void *runtime_stack;
/* a pointer to the bottom of the per-kthread (TLS) runtime stack */
static __thread void *runtime_stack_base;
/* a trap frame to save the original pthread state */
static __thread struct thread_tf pthread_tf;

/* Flag to prevent watchdog from running */
bool disable_watchdog;

/* fast allocation of struct thread */
static struct slab thread_slab;
static struct tcache *thread_tcache;
__thread struct tcache_perthread __perthread_thread_pt;

/* used to track cycle usage in scheduler */
static __thread uint64_t last_tsc;
/* used to force timer and network processing after a timeout */
static __thread uint64_t last_watchdog_tsc;

/**
 * In inc/runtime/thread.h, this function is declared inline (rather than static
 * inline) so that it is accessible to the Rust bindings. As a result, it must
 * also appear in a source file to avoid linker errors.
 */
thread_t *thread_self(void);

/**
 * jmp_thread - runs a thread, popping its trap frame
 * @th: the thread to run
 *
 * This function restores the state of the thread and switches from the runtime
 * stack to the thread's stack. Runtime state is not saved.
 */
static __noreturn void jmp_thread(thread_t *th)
{
    assert_preempt_disabled();
    assert(th->state == THREAD_STATE_RUNNABLE);

    __self = th;
    th->state = THREAD_STATE_RUNNING;
    if (unlikely(load_acquire(&th->stack_busy))) {
        /* wait until the scheduler finishes switching stacks */
        while (load_acquire(&th->stack_busy))
            cpu_relax();
    }
    __jmp_thread(&th->tf);
}

/**
 * jmp_thread_direct - runs a thread, popping its trap frame
 * @oldth: the last thread to run
 * @newth: the next thread to run
 *
 * This function restores the state of the thread and switches from the runtime
 * stack to the thread's stack. Runtime state is not saved.
 */
static void jmp_thread_direct(thread_t *oldth, thread_t *newth)
{
    assert_preempt_disabled();
    assert(newth->state == THREAD_STATE_RUNNABLE);

    __self = newth;
    newth->state = THREAD_STATE_RUNNING;
    if (unlikely(load_acquire(&newth->stack_busy))) {
        /* wait until the scheduler finishes switching stacks */
        while (load_acquire(&newth->stack_busy))
            cpu_relax();
    }
    __jmp_thread_direct(&oldth->tf, &newth->tf, &oldth->stack_busy);
}

/**
 * jmp_runtime - saves the current trap frame and jumps to a function in the
 *               runtime
 * @fn: the runtime function to call
 *
 * WARNING: Only threads can call this function.
 *
 * This function saves state of the running thread and switches to the runtime
 * stack, making it safe to run the thread elsewhere.
 */
static void jmp_runtime(runtime_fn_t fn)
{
    assert_preempt_disabled();
    assert(thread_self() != NULL);

    __jmp_runtime(&thread_self()->tf, fn, runtime_stack);
}

/**
 * jmp_runtime_nosave - jumps to a function in the runtime without saving the
 *            caller's state
 * @fn: the runtime function to call
 */
static __noreturn void jmp_runtime_nosave(runtime_fn_t fn)
{
    assert_preempt_disabled();

    __jmp_runtime_nosave(fn, runtime_stack);
}

/**
 * jmp_runtime_save - saves the pthread frame and jumps to a function in the
 *               runtime
 * @fn: the runtime function to call
 *
 * WARNING: Only the native pthreads should call this function.
 *
 * This function saves state of the native pthread and switches to the runtime
 * stack, so the pthread could terminate itself after restoring the state.
 */
static void jmp_runtime_save(runtime_fn_t fn)
{
    assert_preempt_disabled();

    __jmp_runtime(&pthread_tf, fn, runtime_stack);
}

void ret_pthread()
{
    log_info("kthread returning");
    __jmp_thread(&pthread_tf);
}

static void drain_overflow(struct kthread *l)
{
    thread_t *th;

    assert_spin_lock_held(&l->lock);

    while (l->rq_head - l->rq_tail < RUNTIME_RQ_SIZE) {
        th = list_pop(&l->rq_overflow, thread_t, link);
        if (!th)
            break;
        l->rq[l->rq_head++ % RUNTIME_RQ_SIZE] = th;
        l->q_ptrs->rq_head++;
    }
}

static bool steal_work(struct kthread *l, struct kthread *r)
{
    thread_t *th;
    uint32_t i, avail, rq_tail;

    assert_spin_lock_held(&l->lock);
    assert(l->rq_head == 0 && l->rq_tail == 0);

    if (!spin_try_lock(&r->lock))
        return false;

    /* harmless race condition */
    if (unlikely(r->detached)) {
        spin_unlock(&r->lock);
        return false;
    }

    /* try to steal directly from the runqueue */
    avail = load_acquire(&r->rq_head) - r->rq_tail;
    if (avail) {
        /* steal half the tasks */
        avail = div_up(avail, 2);
        assert(avail <= div_up(RUNTIME_RQ_SIZE, 2));
        rq_tail = r->rq_tail;
        for (i = 0; i < avail; i++)
            l->rq[i] = r->rq[rq_tail++ % RUNTIME_RQ_SIZE];
        store_release(&r->rq_tail, rq_tail);
        r->q_ptrs->rq_tail += avail;
        spin_unlock(&r->lock);

        l->rq_head = avail;
        l->q_ptrs->rq_head += avail;
        return true;
    }

    /* check for overflow tasks */
    th = list_pop(&r->rq_overflow, thread_t, link);
    if (th)
        goto done;

    /* check for softirqs */
    th = softirq_run_thread(r, RUNTIME_SOFTIRQ_BUDGET);
    if (th) {
        goto done;
    }

done:
    /* either enqueue the stolen work or detach the kthread */
    if (th) {
        l->rq[l->rq_head++] = th;
        l->q_ptrs->rq_head++;
    } else if (r->parked) {
        kthread_detach(r);
    }

    spin_unlock(&r->lock);
    return th != NULL;
}

static __noinline thread_t *do_watchdog(struct kthread *l)
{
    thread_t *th;

    assert_spin_lock_held(&l->lock);

    /* then check the network queues */
    th = softirq_run_thread(l, RUNTIME_SOFTIRQ_BUDGET);
    if (th) {
        return th;
    }

    return NULL;
}

/* the main scheduler routine, decides what to run next */
static __noreturn __noinline void schedule(void)
{
    struct kthread *r = NULL, *l = myk();
    uint64_t start_tsc, end_tsc;
    thread_t *th = NULL;
    unsigned int last_nrks;
    unsigned int iters = 0;
    int i, sibling;

    assert_spin_lock_held(&l->lock);
    assert(l->parked == false);
    assert(l->detached == false);

    /* unmark busy for the stack of the last uthread */
    if (__self != NULL) {
        store_release(&__self->stack_busy, false);
        __self = NULL;
    }

    /* detect misuse of preempt disable */
    BUG_ON((preempt_cnt & ~PREEMPT_NOT_PENDING) != 1);

    /* update entry stat counters */
    start_tsc = libut_rdtsc();

    /* increment the RCU generation number (even is in scheduler) */
    store_release(&l->rcu_gen, l->rcu_gen + 1);
    assert((l->rcu_gen & 0x1) == 0x0);

    /* if it's been too long, run the softirq handler */
    if (!disable_watchdog &&
        unlikely(start_tsc - last_watchdog_tsc >
                 cycles_per_us * RUNTIME_WATCHDOG_US)) {
        last_watchdog_tsc = start_tsc;
        th = do_watchdog(l);
        if (th)
            goto done;
    }

again:
    /* move overflow tasks into the runqueue */
    if (unlikely(!list_empty(&l->rq_overflow)))
        drain_overflow(l);

    /* first try the local receive runqueue, see if any targeted task */
    if (l->rrq_head != l->rrq_tail) {
        th = l->rrq[l->rrq_tail++ % RUNTIME_RRQ_SIZE];
        goto done;
    }

    /* reset the local receive runqueue since it's empty */
    l->rrq_head = l->rrq_tail = 0;

    /* then try the local runqueue */
    if (l->rq_head != l->rq_tail)
        goto done;

    /* reset the local runqueue since it's empty */
    l->rq_head = l->rq_tail = 0;

    /* then check for local softirqs */
    th = softirq_run_thread(l, RUNTIME_SOFTIRQ_BUDGET);
    if (th) {
        goto done;
    }

    last_nrks = load_acquire(&nrks);

    i = 0;
    /* then try to steal from a thread-sibling kthread */
    while (RUNTIME_MAX_SIBLINGS > i &&
           0 != cpu_map[l->curr_cpu].sibling_core[i]) {
        sibling = cpu_map[l->curr_cpu].sibling_core[i] - 1;
        r = cpu_map[sibling].recent_kthread;
        if (r && r != l && steal_work(l, r))
            goto done;
        ++i;
    }

    if (RUNTIME_SCHED_POLL_LVL1 <= iters)
    {
        /* then try to steal from a cache-sibling kthread */
        while (RUNTIME_MAX_SIBLINGS > ++i &&
               0 != cpu_map[l->curr_cpu].sibling_core[i]) {
            sibling = cpu_map[l->curr_cpu].sibling_core[i] - 1;
            r = cpu_map[sibling].recent_kthread;
            if (r && r != l && steal_work(l, r))
                goto done;
        }

        /* then try to steal from a random kthread */
        r = ks[rand_crc32c((uintptr_t)l) % last_nrks];
        if (r != l && steal_work(l, r))
            goto done;

    }

    if (RUNTIME_SCHED_POLL_LVL2 <= iters)
    {
        /* finally try to steal from every kthread */
        for (i = 0; i < last_nrks; i++)
            if (ks[i] != l && steal_work(l, ks[i]))
                goto done;
    }

    /* keep trying to find work until the polling timeout expires */
    if (!preempt_needed() &&
        (++iters < RUNTIME_SCHED_POLL_MAX ||
         libut_rdtsc() - start_tsc <
         cycles_per_us * RUNTIME_SCHED_MIN_POLL_US))
        goto again;

    /* did not find anything to run, park this kthread */
    /* we may have got a preempt signal before voluntarily yielding */
    kthread_park(!preempt_needed());
    start_tsc = libut_rdtsc();

    goto again;

done:
    /* pop off a thread and run it */
    if (!th) {
        assert(l->rq_head != l->rq_tail);
        th = l->rq[l->rq_tail++ % RUNTIME_RQ_SIZE];
        l->q_ptrs->rq_tail++;
    }

    /* move overflow tasks into the runqueue */
    if (unlikely(!list_empty(&l->rq_overflow)))
        drain_overflow(l);

    spin_unlock(&l->lock);

    /* update exit stat counters */
    end_tsc = libut_rdtsc();
    last_tsc = end_tsc;

    /* increment the RCU generation number (odd is in thread) */
    store_release(&l->rcu_gen, l->rcu_gen + 1);
    assert((l->rcu_gen & 0x1) == 0x1);

    /* and jump into the next thread */
    jmp_thread(th);
}

/**
 * join_kthread - detaches a kthread immediately (rather than through stealing)
 * @k: the kthread to detach
 *
 * Can and must be called from thread context.
 */
void join_kthread(struct kthread *k)
{
    thread_t *waketh;
    struct list_head tmp;

    //log_info_ratelimited("join_kthread() %p", k);

    list_head_init(&tmp);

    /* if the lock can't be acquired, the kthread is unparking */
    if (!spin_try_lock_np(&k->lock))
        return;

    /* harmless race conditions */
    if (k->detached || !k->parked || k == myk()) {
        spin_unlock_np(&k->lock);
        return;
    }

    /* drain the runqueue */
    for (; k->rq_tail < k->rq_head; k->rq_tail++) {
        list_add_tail(&tmp, &k->rq[k->rq_tail % RUNTIME_RQ_SIZE]->link);
        k->q_ptrs->rq_tail++;
    }
    k->rq_head = k->rq_tail = 0;

    /* drain the overflow runqueue */
    list_append_list(&tmp, &k->rq_overflow);

    /* detach the kthread */
    kthread_detach(k);
    spin_unlock_np(&k->lock);

    /* re-wake all the runnable threads belonging to the detached kthread */
    while (true) {
        waketh = list_pop(&tmp, thread_t, link);
        if (!waketh)
            break;
        waketh->state = THREAD_STATE_SLEEPING;
        thread_ready(waketh);
    }
}

static __always_inline void enter_schedule(thread_t *myth)
{
    struct kthread *k = myk();
    thread_t *th;

    assert_preempt_disabled();

    /* lock-free path: get a task targetted for this kthread */
    if (unlikely(k->rrq_head != k->rrq_tail)) {
        th = k->rrq[k->rrq_tail % RUNTIME_RRQ_SIZE];
        if (likely(!th->stack_busy)) { /* has been released */
            k->rrq_tail++;
            jmp_thread_direct(myth, th);
            return;
        } /* else if stack_busy, could dead-lock in jmp_thread_direct */
    }

    spin_lock(&k->lock);

    /* slow path: switch from the uthread stack to the runtime stack */
    if (k->rq_head == k->rq_tail ||
        unlikely(k->rrq_head != k->rrq_tail) || /* might have targeted task */
        (!disable_watchdog &&
         unlikely(libut_rdtsc() - last_watchdog_tsc >
              cycles_per_us * RUNTIME_WATCHDOG_US))) {
        jmp_runtime(schedule);
        return;
    }

    /* fast path: switch directly to the next uthread */

    /* pop the next runnable thread from the queue */
    th = k->rq[k->rq_tail++ % RUNTIME_RQ_SIZE];
    k->q_ptrs->rq_tail++;
    spin_unlock(&k->lock);

    /* increment the RCU generation number (odd is in thread) */
    store_release(&k->rcu_gen, k->rcu_gen + 2);
    assert((k->rcu_gen & 0x1) == 0x1);

    /* check for misuse of preemption disabling */
    BUG_ON((preempt_cnt & ~PREEMPT_NOT_PENDING) != 1);

    /* check if we're switching into the same thread as before */
    if (unlikely(th == myth)) {
        th->state = THREAD_STATE_RUNNING;
        th->stack_busy = false;
        preempt_enable();
        return;
    }

    /* switch stacks and enter the next thread */
    jmp_thread_direct(myth, th);
}

/**
 * thread_park_and_unlock_np - puts a thread to sleep, unlocks the lock @l,
 * and schedules the next thread
 * @l: the lock to be released
 */
void thread_park_and_unlock_np(spinlock_t *l)
{
    thread_t *myth = thread_self();

    assert_preempt_disabled();
    assert_spin_lock_held(l);
    assert(myth->state == THREAD_STATE_RUNNING);

    myth->state = THREAD_STATE_SLEEPING;
    myth->stack_busy = true;
    spin_unlock(l);

    enter_schedule(myth);
}

/**
 * thread_yield - yields the currently running thread
 *
 * Yielding will give other threads a chance to run.
 */
void thread_yield(void)
{
    thread_t *myth = thread_self();

    /* check for softirqs */
    softirq_run(RUNTIME_SOFTIRQ_BUDGET);

    preempt_disable();
    assert(myth->state == THREAD_STATE_RUNNING);
    myth->state = THREAD_STATE_SLEEPING;
    store_release(&myth->stack_busy, true);
    thread_throw(myth, myth->kthread_wanted);

    enter_schedule(myth);
}

/**
 * thread_ready - marks a thread as a runnable
 * @th: the thread to mark runnable
 *
 * This function can only be called when @th is sleeping.
 */
void thread_ready(thread_t *th)
{
    struct kthread *k;
    uint32_t rq_tail;

    assert(th->state == THREAD_STATE_SLEEPING);
    th->state = THREAD_STATE_RUNNABLE;

    k = getk();
    rq_tail = load_acquire(&k->rq_tail);
    if (unlikely(k->rq_head - rq_tail >= RUNTIME_RQ_SIZE)) {
        assert(k->rq_head - rq_tail == RUNTIME_RQ_SIZE);
        spin_lock(&k->lock);
        list_add_tail(&k->rq_overflow, &th->link);
        spin_unlock(&k->lock);
        putk();
        return;
    }

    k->rq[k->rq_head % RUNTIME_RQ_SIZE] = th;
    store_release(&k->rq_head, k->rq_head + 1);
    k->q_ptrs->rq_head++;
    putk();
}

/**
 * thread_throw - try adding work for the kthread on the given core
 * @th: the thread to mark runnable
 * @kidx: the id of the kthread, -1 to use the current kthread
 *
 * This function can only be called when @th is sleeping.
 */
void thread_throw(thread_t *th, int kidx)
{
    struct kthread *r;

    if (-1 == kidx) {
        thread_ready(th);
        return;
    }

    if (th->kthread_wanted != kidx)
        th->kthread_wanted = kidx;

    kidx %= maxks;
    r = allks[kidx];
    if (myk() == r) {
        thread_ready(th);
        return;
    }

    if (r && spin_try_lock_np(&r->lock)) {
        if (likely(!(r->detached || r->parked))) {
            th->state = THREAD_STATE_RUNNABLE;
            if (unlikely(r->rrq_head - r->rrq_tail >= RUNTIME_RRQ_SIZE)) {
                list_add_tail(&r->rq_overflow, &th->link);
            } else {
                r->rrq[r->rrq_head % RUNTIME_RRQ_SIZE] = th;
                store_release(&r->rrq_head, r->rrq_head + 1);
            }
            spin_unlock_np(&r->lock);
            return;
        }
        spin_unlock_np(&r->lock);
    }
    thread_ready(th);
    return;
}

/**
 * thread_swap - swap current uthread with the given one
 * @th: the thread to swap in
 * @kidx: the id of the kthread, -1 to use the current kthread
 *
 * This function can only be called when @th is sleeping.
 */
void thread_swap(thread_t *th, int kidx)
{
    thread_t *myth = thread_self();

    preempt_disable();
    assert(myth->state == THREAD_STATE_RUNNING);
    myth->state = THREAD_STATE_SLEEPING;
    store_release(&myth->stack_busy, true);
    thread_throw(myth, kidx);

    assert(th->state == THREAD_STATE_SLEEPING);
    th->state = THREAD_STATE_RUNNABLE;
    jmp_thread_direct(myth, th);
}

static void thread_finish_yield_kthread(void)
{
    struct kthread *k = myk();
    thread_t *myth = thread_self();

    assert(myth->state == THREAD_STATE_RUNNING);
    myth->state = THREAD_STATE_SLEEPING;
    thread_ready(myth);

    store_release(&k->rcu_gen, k->rcu_gen + 1);
    spin_lock(&k->lock);
    clear_preempt_needed();
    kthread_park(false);
    last_tsc = libut_rdtsc();
    store_release(&k->rcu_gen, k->rcu_gen + 1);

    schedule();
}

/**
 * thread_yield_kthread - yields the running thread and immediately parks
 */
void thread_yield_kthread(void)
{
    /* this will switch from the thread stack to the runtime stack */
    preempt_disable();
    jmp_runtime(thread_finish_yield_kthread);
}

static __always_inline thread_t *__thread_create(void)
{
    thread_t *th;
    struct stack *s;

    preempt_disable();
    th = tcache_alloc(&__perthread_thread_pt);
    if (unlikely(!th)) {
        preempt_enable();
        return NULL;
    }

    s = stack_alloc();
    if (unlikely(!s)) {
        tcache_free(&__perthread_thread_pt, th);
        preempt_enable();
        return NULL;
    }
    preempt_enable();

    th->stack = s;
    th->state = THREAD_STATE_SLEEPING;
    th->main_thread = false;

    return th;
}

/**
 * thread_create - creates a new thread
 * @fn: a function pointer to the starting method of the thread
 * @arg: an argument passed to @fn
 *
 * Returns 0 if successful, otherwise -ENOMEM if out of memory.
 */
thread_t *thread_create(thread_fn_t fn, void *arg)
{
    thread_t *th = __thread_create();
    if (unlikely(!th))
        return NULL;

    SPTR(&(th->tf)) = stack_init_to_rsp(th->stack, thread_exit);
#if defined(__aarch64__)
    LPTR(&(th->tf)) = (uint64_t)thread_exit;
#endif
    ARG0(&(th->tf)) = (uint64_t)arg;
    BPTR(&(th->tf)) = (uint64_t)0; /* just in case base pointers are enabled */
    IPTR(&(th->tf)) = (uint64_t)fn;
    th->stack_busy = false;
    return th;
}

/**
 * thread_create_with_buf - creates a new thread with space for a buffer on the
 * stack
 * @fn: a function pointer to the starting method of the thread
 * @buf: a pointer to the stack allocated buffer (passed as arg too)
 * @buf_len: the size of the stack allocated buffer
 *
 * Returns 0 if successful, otherwise -ENOMEM if out of memory.
 */
thread_t *thread_create_with_buf(thread_fn_t fn, void **buf, size_t buf_len)
{
    void *ptr;
    thread_t *th = __thread_create();
    if (unlikely(!th))
        return NULL;

    SPTR(&(th->tf)) = stack_init_to_rsp_with_buf(th->stack, &ptr,
                        buf_len, thread_exit);
#if defined(__aarch64__)
    LPTR(&(th->tf)) = (uint64_t)thread_exit;
#endif
    ARG0(&(th->tf)) = (uint64_t)ptr;
    BPTR(&(th->tf)) = (uint64_t)0; /* just in case base pointers are enabled */
    IPTR(&(th->tf)) = (uint64_t)fn;
    th->stack_busy = false;
    *buf = ptr;
    return th;
}

/**
 * thread_spawn - creates and launches a new thread
 * @fn: a function pointer to the starting method of the thread
 * @arg: an argument passed to @fn
 *
 * Returns 0 if successful, otherwise -ENOMEM if out of memory.
 */
int thread_spawn(thread_fn_t fn, void *arg)
{
    thread_t *th = thread_create(fn, arg);
    if (unlikely(!th))
        return -ENOMEM;
    thread_ready(th);
    return 0;
}

/**
 * thread_spawn_main - creates and launches the main thread
 * @fn: a function pointer to the starting method of the thread
 * @arg: an argument passed to @fn
 *
 * WARNING: Only can be called once.
 *
 * Returns 0 if successful, otherwise -ENOMEM if out of memory.
 */
int thread_spawn_main(thread_fn_t fn, void *arg)
{
    static bool called = false;
    thread_t *th;

    BUG_ON(called);
    called = true;

    th = thread_create(fn, arg);
    if (!th)
        return -ENOMEM;
    th->main_thread = true;
    thread_ready(th);
    return 0;
}

static void thread_finish_exit(void)
{
    thread_t *th = thread_self();

    /* if the main thread dies, kill the whole program */
    if (unlikely(th->main_thread)) {
        pthread_t my_tid = pthread_self();
        int idx = 0;
        for (; maxks > idx; ++idx) {
            if (0 == ktids[idx] || ktids[idx] == my_tid) {
                continue;
            }
            if (unlikely(pthread_kill(ktids[idx], SIGUSR2) < 0)) {
                WARN();
            }
        }
        for (idx = 0; maxks > idx; ++idx) {
            if (0 == ktids[idx] || ktids[idx] == my_tid) {
                continue;
            }
            pthread_join(ktids[idx], NULL);
        }
        ret_pthread();
        return;
    }
    stack_free(th->stack);
    tcache_free(&__perthread_thread_pt, th);
    __self = NULL;

    spin_lock(&myk()->lock);
    schedule();
}

/**
 * thread_exit - terminates a thread
 */
void thread_exit(void)
{
    /* can't free the stack we're currently using, so switch */
    preempt_disable();
    jmp_runtime_nosave(thread_finish_exit);
}

/**
 * immediately park each kthread when it first starts up, only schedule it once
 * the iokernel has granted it a core
 */
static __noreturn void schedule_start(void)
{
    struct kthread *k = myk();

    /*
     * force kthread parking (iokernel assumes all kthreads are parked
     * initially). Update RCU generation so it stays even after entering
     * schedule().
     */
    kthread_wait_to_attach();
    store_release(&k->rcu_gen, 1);

    spin_lock(&k->lock);
    schedule();
}

/**
 * sched_start - used only to enter the runtime the first time
 */
void sched_start(void)
{
    last_tsc = libut_rdtsc();
    preempt_disable();
    jmp_runtime_save(schedule_start);
}

static void runtime_top_of_stack(void)
{
    panic("a thread returned to the top of the stack");
}

/**
 * sched_init_thread - initializes per-thread state for the scheduler
 *
 * Returns 0 if successful, or -ENOMEM if out of memory.
 */
int sched_init_thread(void)
{
    struct stack *s;

    tcache_init_perthread(thread_tcache, &__perthread_thread_pt);

    s = stack_alloc();
    if (!s)
        return -ENOMEM;

    runtime_stack_base = (void *)s;
    runtime_stack = (void *)stack_init_to_rsp(s, runtime_top_of_stack); 

    return 0;
}

/**
 * sched_init - initializes the scheduler subsystem
 *
 * Returns 0 if successful, or -ENOMEM if out of memory.
 */
int sched_init(void)
{
    int ret, i, j, siblings;

    /*
     * set up allocation routines for threads
     */
    ret = slab_create(&thread_slab, "runtime_threads",
              sizeof(thread_t), 0);
    if (ret)
        return ret;

    thread_tcache = slab_create_tcache(&thread_slab,
                       TCACHE_DEFAULT_MAG_SIZE);
    if (!thread_tcache) {
        slab_destroy(&thread_slab);
        return -ENOMEM;
    }

    for (i = 0; i < cpu_count; i++) {
        siblings = 0;
        bitmap_for_each_set(cpu_info_tbl[i].thread_siblings_mask,
                    cpu_count, j) {
            if (i == j)
                continue;
            BUG_ON(RUNTIME_MAX_SIBLINGS <= siblings);
            cpu_map[i].sibling_core[siblings++] = j + 1; /* 1-indexed */
        }

        if (RUNTIME_MAX_SIBLINGS <= siblings)
            continue;
        cpu_map[i].sibling_core[siblings++] = 0; /* 0 as a seperator */
        if (RUNTIME_MAX_SIBLINGS <= siblings)
            continue;

        for (j = i;
             j = bitmap_find_next_set(cpu_info_tbl[i].l2_siblings_mask,
                                      cpu_count, j + 1),
             j < cpu_count;) {
            if (bitmap_test(cpu_info_tbl[i].thread_siblings_mask, j))
                continue;
            cpu_map[i].sibling_core[siblings++] = j + 1; /* 1-indexed */
            if (RUNTIME_MAX_SIBLINGS <= siblings)
                break;
        }
        bitmap_for_each_set(cpu_info_tbl[i].l2_siblings_mask, i, j) {
            if (bitmap_test(cpu_info_tbl[i].thread_siblings_mask, j))
                continue;
            cpu_map[i].sibling_core[siblings++] = j + 1; /* 1-indexed */
            if (RUNTIME_MAX_SIBLINGS <= siblings)
                break;
        }
    }

    return 0;
}

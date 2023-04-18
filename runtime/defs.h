/*
 * defs.h - internal runtime definitions
 */

#pragma once

#include <base/stddef.h>
#include <base/list.h>
#include <base/mem.h>
#include <base/tcache.h>
#include <base/gen.h>
#include <base/lrpc.h>
#include <base/thread.h>
#include <base/time.h>
#include <hwalloc/control.h>
#include <hwalloc/queue.h>
#include <runtime/thread.h>
#include <runtime/preempt.h>


/*
 * constant limits
 * TODO: make these configurable?
 */

#define RUNTIME_MAX_THREADS       100000
#define RUNTIME_STACK_SIZE        128 * KB
#define RUNTIME_GUARD_SIZE        128 * KB
#define RUNTIME_RQ_SIZE           32
#define RUNTIME_RRQ_SIZE          7
#define RUNTIME_SOFTIRQ_BUDGET    16
#define RUNTIME_MAX_TIMERS        4096
#define RUNTIME_MAX_SIBLINGS      7
#define RUNTIME_SCHED_POLL_LVL1   1
#define RUNTIME_SCHED_POLL_LVL2   3
#define RUNTIME_SCHED_POLL_MAX    4
#define RUNTIME_SCHED_MIN_POLL_US 2
#define RUNTIME_WATCHDOG_US       50


/*
 * Trap frame support
 */

#if defined(__x86_64__)
/*
 * See the "System V Application Binary Interface" for a full explation of
 * calling and argument passing conventions.
 */

struct thread_tf {
    /* argument registers, can be clobbered by callee */
    uint64_t rdi; /* first argument */
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;

    /* callee-saved registers */
    uint64_t rbx;
    uint64_t rbp; /* frame pointer */
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    /* special-purpose registers */
    uint64_t rax;    /* holds return value */
    uint64_t rip;    /* instruction pointer */
    uint64_t rsp;    /* stack pointer */
};

#define SPTR(tf)        ((tf)->rsp)
#define IPTR(tf)        ((tf)->rip)
#define BPTR(tf)        ((tf)->rbp)
#define ARG0(tf)        ((tf)->rdi)
#define ARG1(tf)        ((tf)->rsi)
#define ARG2(tf)        ((tf)->rdx)
#define ARG3(tf)        ((tf)->rcx)
#define ARG4(tf)        ((tf)->r8)
#define ARG5(tf)        ((tf)->r9)

#elif defined(__aarch64__)

struct thread_tf {
    /* argument registers, can be clobbered by callee */
    uint64_t r0; /* first argument */
    /* no space needed for r1-r18, which are clobbered
    uint64_t r1;
    uint64_t r2;
    uint64_t r3;
    uint64_t r4;
    uint64_t r5;
    uint64_t r6;
    uint64_t r7;
    */

    /* special register */
    /*uint64_t r8; */    /* indirect result location register */

    /* temporary registers */
    /*
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    */

    /* special registers */
    /*uint64_t r16;*/    /* IP0 first intra-procedure-call scratch register */
    /*uint64_t r17;*/    /* IP1 second intra-procedure-call scratch register */
    /*uint64_t r18;*/    /* platform register */

    /* callee-saved registers */
    uint64_t r19;
    uint64_t r20;
    uint64_t r21;
    uint64_t r22;
    uint64_t r23;
    uint64_t r24;
    uint64_t r25;
    uint64_t r26;
    uint64_t r27;
    uint64_t r28;

    /* special-purpose registers */
    uint64_t r29;    /* FP the frame pointer */
    uint64_t r30;    /* LR the linker register */
    uint64_t pc;    /* IP the frame pointer */
    uint64_t sp;    /* sP the stack pointer */

#if defined(__ARM_NEON)
    /* SIMD&FP registers */
    uint64_t v8_v15[16];
#endif
};

#define SPTR(tf)        ((tf)->sp)
#define IPTR(tf)        ((tf)->pc)
#define BPTR(tf)        ((tf)->r29)
#define LPTR(tf)        ((tf)->r30)
#define ARG0(tf)        ((tf)->r0)

#endif


/*
 * Thread support
 */

enum {
    THREAD_STATE_RUNNING = 0,
    THREAD_STATE_RUNNABLE,
    THREAD_STATE_SLEEPING,
};

struct stack;

struct libut_thread {
    struct thread_tf    tf;
    struct list_node    link;
    struct stack        *stack;
    unsigned int        main_thread:1;
    unsigned int        state;
    unsigned int        stack_busy;
    int                 kthread_wanted;
};

typedef void (*runtime_fn_t)(void);

/* assembly helper routines from switch.S */
extern void __jmp_thread(struct thread_tf *tf) __noreturn;
extern void __jmp_thread_direct(struct thread_tf *oldtf,
                struct thread_tf *newtf,
                unsigned int *stack_busy);
extern void __jmp_runtime(struct thread_tf *tf, runtime_fn_t fn,
              void *stack);
extern void __jmp_runtime_nosave(runtime_fn_t fn, void *stack) __noreturn;


/*
 * Stack support
 */

#define STACK_PTR_SIZE    (RUNTIME_STACK_SIZE / sizeof(uintptr_t))
#define GUARD_PTR_SIZE    (RUNTIME_GUARD_SIZE / sizeof(uintptr_t))

struct stack {
    uintptr_t    usable[STACK_PTR_SIZE];
    uintptr_t    guard[GUARD_PTR_SIZE]; /* unreadable and unwritable */
};

extern __thread struct tcache_perthread __perthread_stack_pt;

/**
 * stack_alloc - allocates a stack
 *
 * Stack allocation is extremely cheap, think less than taking a lock.
 *
 * Returns an unitialized stack.
 */
static inline struct stack *stack_alloc(void)
{
    return tcache_alloc(&perthread_get(stack_pt));
}

/**
 * stack_free - frees a stack
 * @s: the stack to free
 */
static inline void stack_free(struct stack *s)
{
    tcache_free(&perthread_get(stack_pt), (void *)s);
}

#define RSP_ALIGNMENT    16

static inline void assert_rsp_aligned(uint64_t rsp)
{
#if defined(__x86_64__)
    /*
     * The stack must be 16-byte aligned at process entry according to
     * the System V Application Binary Interface (section 3.4.1).
     *
     * The callee assumes a return address has been pushed on the aligned
     * stack by CALL, so we look for an 8 byte offset.
     */
    assert(rsp % RSP_ALIGNMENT == sizeof(void *));
#elif defined(__aarch64__)
    assert(rsp % RSP_ALIGNMENT == 0);
#endif
}

/**
 * stack_init_to_rsp - sets up an exit handler and returns the top of the stack
 * @s: the stack to initialize
 * @exit_fn: exit handler that is called when the top of the call stack returns
 *
 * Returns the top of the stack as a stack pointer.
 */
static inline uint64_t stack_init_to_rsp(struct stack *s, void (*exit_fn)(void))
{
    uint64_t rsp;

#if defined(__x86_64__)
    s->usable[STACK_PTR_SIZE - 1] = (uintptr_t)exit_fn;
    rsp = (uint64_t)&s->usable[STACK_PTR_SIZE - 1];
#elif defined(__aarch64__)
    s->usable[STACK_PTR_SIZE - 2] = (uintptr_t)exit_fn;
    rsp = (uint64_t)&s->usable[STACK_PTR_SIZE - 2];
#endif
    assert_rsp_aligned(rsp);
    return rsp;
}

/**
 * stack_init_to_rsp_with_buf - sets up an exit handler and returns the top of
 * the stack, reserving space for a buffer above
 * @s: the stack to initialize
 * @buf: a pointer to store the buffer pointer
 * @buf_len: the length of the buffer to reserve
 * @exit_fn: exit handler that is called when the top of the call stack returns
 *
 * Returns the top of the stack as a stack pointer.
 */
static inline uint64_t
stack_init_to_rsp_with_buf(struct stack *s, void **buf, size_t buf_len,
               void (*exit_fn)(void))
{
    uint64_t rsp, pos = STACK_PTR_SIZE;

    /* reserve the buffer */
    pos -= div_up(buf_len, sizeof(uint64_t));
    pos = align_down(pos, RSP_ALIGNMENT / sizeof(uint64_t));
    *buf = (void *)&s->usable[pos];

    /* setup for usage as stack */
#if defined(__x86_64__)
    s->usable[--pos] = (uintptr_t)exit_fn;
    rsp = (uint64_t)&s->usable[pos];
#elif defined(__aarch64__)
    s->usable[pos-2] = (uintptr_t)exit_fn;
    rsp = (uint64_t)&s->usable[pos-2];
#endif
    assert_rsp_aligned(rsp);
    return rsp;
}

/*
 * ioqueues
 */

DECLARE_SPINLOCK(qlock);
extern unsigned int nrqs;

struct hwallocd_control {
    int fd;
    mem_key_t key;
    unsigned int thread_count;
    struct thread_spec threads[NCPU];
};

extern struct hwallocd_control hwc;


/*
 * Per-kernel-thread State
 */

struct timer_idx;

struct kthread {
    /* 1st cache-line */
    spinlock_t             lock;
    uint32_t               generation;
    uint32_t               rq_head;
    uint32_t               rq_tail;
    struct list_head       rq_overflow;
    struct lrpc_chan_in    rxq;
    int                    park_efd;
    unsigned int           parked:1;
    unsigned int           detached:1;

    /* 2nd cache-line */
    struct q_ptrs          *q_ptrs;
    //struct mbufq           txpktq_overflow[16];
    //struct mbufq           txcmdq_overflow[16];
    unsigned int           rcu_gen;
    unsigned int           curr_cpu;
    uint64_t               park_us;
    int                    allks_idx;
    unsigned int           pad1[1];
    struct lrpc_chan_out   txq;

    /* 3rd cache-line */
    //struct lrpc_chan_out   txpktq[32];

    /* 4th-7th cache-line */
    thread_t               *rq[RUNTIME_RQ_SIZE];
    /* 8th cache-line */
    thread_t               *rrq[RUNTIME_RRQ_SIZE];
    uint32_t               rrq_head;
    uint32_t               rrq_tail;

    /* 9th cache-line */
    spinlock_t             timer_lock;
    unsigned int           timern;
    struct timer_idx       *timers;
    unsigned long          pad2[6];
};

/* compile-time verification of cache-line alignment */
BUILD_ASSERT(offsetof(struct kthread, lock) % CACHE_LINE_SIZE == 0);
BUILD_ASSERT(offsetof(struct kthread, q_ptrs) % CACHE_LINE_SIZE == 0);
//BUILD_ASSERT(offsetof(struct kthread, txpktq) % CACHE_LINE_SIZE == 0);
BUILD_ASSERT(offsetof(struct kthread, rq) % CACHE_LINE_SIZE == 0);
BUILD_ASSERT(offsetof(struct kthread, rrq) % CACHE_LINE_SIZE == 0);
BUILD_ASSERT(offsetof(struct kthread, timer_lock) % CACHE_LINE_SIZE == 0);

extern __thread struct kthread *mykthread;

/**
 * myk - returns the per-kernel-thread data
 */
static inline struct kthread *myk(void)
{
    return mykthread;
}

/**
 * getk - returns the per-kernel-thread data and disables preemption
 *
 * WARNING: If you're using myk() instead of getk(), that's a bug if preemption
 * is enabled. The local kthread can change at anytime.
 */
static inline struct kthread *getk(void)
{
    preempt_disable();
    return mykthread;
}

/**
 * putk - reenables preemption after calling getk()
 */
static inline void putk(void)
{
    preempt_enable();
}

DECLARE_SPINLOCK(klock);
extern unsigned int maxks;
extern unsigned int spinks;
extern unsigned int guaranteedks;
extern unsigned int nrks;
extern struct kthread *ks[NCPU];
extern struct kthread *allks[NCPU];
extern pthread_t ktids[NCPU];

extern void kthread_detach(struct kthread *r);
extern void kthread_park(bool voluntary);
extern void kthread_wait_to_attach(void);

struct cpu_record {
    struct kthread *recent_kthread;
    unsigned long sibling_core[RUNTIME_MAX_SIBLINGS]; /* 1-indexed */
};

BUILD_ASSERT(sizeof(struct cpu_record) == CACHE_LINE_SIZE);

extern struct cpu_record cpu_map[NCPU];



/*
 * Softirq support
 */

/* the maximum number of events to handle in a softirq invocation */
#define SOFTIRQ_MAX_BUDGET    128

extern bool disable_watchdog;

extern thread_t *softirq_run_thread(struct kthread *k, unsigned int budget);
extern void softirq_run(unsigned int budget);


/*
 * Network stack
 */

struct net_cfg {
    struct shm_region    tx_region;
    uint8_t              pad[48];
};

BUILD_ASSERT(sizeof(struct net_cfg) == CACHE_LINE_SIZE);

extern struct net_cfg netcfg;

extern void __net_recurrent(void);


/*
 * Timer support
 */

extern void timer_softirq(struct kthread *k, unsigned int budget);
extern void timer_merge(struct kthread *r);
extern uint64_t timer_earliest_deadline(void);

struct timer_idx {
    uint64_t               deadline_us;
    struct timer_entry    *e;
};

/**
 * timer_needed - returns true if pending timers have to be handled
 * @k: the kthread to check
 */
static inline bool timer_needed(struct kthread *k)
{
    /* deliberate race condition */
    return k->timern > 0 && k->timers[0].deadline_us <= microtime();
}


/*
 * Init
 */

/* global initialization */
extern int ioqueues_init(void);
extern int stack_init(void);
extern int sched_init(void);
extern int preempt_init(void);
extern int trans_init(void);

/* per-thread initialization */
extern int kthread_init_thread(void);
extern int ioqueues_init_thread(void);
extern int stack_init_thread(void);
extern int timer_init_thread(void);
extern int sched_init_thread(void);

/* per-thread finialization */
extern int timer_fini_thread(void);
extern int kthread_fini_thread(void);

/* late initialization */
extern int ioqueues_register_hwallocd(void);

/* configuration loading */
extern int cfg_load(const char *path);

/* runtime entry helpers */
extern void sched_start(void);
extern int thread_spawn_main(thread_fn_t fn, void *arg);
extern void thread_yield_kthread();
extern void join_kthread(struct kthread *k);
extern void ret_pthread();

/*
 * init.c - initializes the runtime
 */

#include <pthread.h>

#include <base/cpu.h>
#include <base/init.h>
#include <base/log.h>
#include <base/limits.h>
#include <runtime/thread.h>

#include "defs.h"

static pthread_barrier_t init_barrier;

static initializer_fn_t global_init_hook = NULL;
static initializer_fn_t perthread_init_hook = NULL;
static initializer_fn_t late_init_hook = NULL;

/* global subsystem initialization */
static const struct init_handler global_init_handlers[] = {
    /* runtime core */
    BASE_INITIALIZER(ioqueues), /* map shm and allocate txq/rxq */
    BASE_INITIALIZER(stack),    /* create stack_tcache */
    BASE_INITIALIZER(sched),    /* create thread_tcache */
    BASE_INITIALIZER(preempt),  /* register handler for SIGUSR1 */
};

/* per-kthread subsystem initialization */
static const struct init_handler thread_init_handlers[] = {
    /* runtime core */
    THREAD_INITIALIZER(kthread),  /* allocate and populate kthread */
    THREAD_INITIALIZER(ioqueues), /* set thread_specs and create queue pairs */
    THREAD_INITIALIZER(stack),    /* init perthread stack tcache */
    THREAD_INITIALIZER(timer),    /* allocate cacheline timer structure */
    THREAD_INITIALIZER(sched),    /* set perthread thread tcache, init rsp */
};

static const struct init_handler late_init_handlers[] = {
};

static int runtime_init_thread(void)
{
    int ret;

    ret = base_init_thread();
    if (ret) {
        log_err("base library per-thread init failed, ret = %d", ret);
        return ret;
    }

    ret = run_init_handlers("per-thread", thread_init_handlers,
                 ARRAY_SIZE(thread_init_handlers));
    if (ret || perthread_init_hook == NULL)
        return ret;

    return perthread_init_hook();

}

static void *pthread_entry(void *data)
{
    int ret;

    ret = runtime_init_thread();
    BUG_ON(ret);

    pthread_barrier_wait(&init_barrier);
    sched_start();

    /* never reached unless things are broken */
    BUG();
    return NULL;
}

/**
 * runtime_set_initializers - allow runtime to specifcy a function to run in
 * each stage of intialization (called before runtime_init).
 */
int runtime_set_initializers(initializer_fn_t global_fn,
                 initializer_fn_t perthread_fn,
                 initializer_fn_t late_fn)
{
    global_init_hook = global_fn;
    perthread_init_hook = perthread_fn;
    late_init_hook = late_fn;
    return 0;
}

/**
 * runtime_init - starts the runtime
 * @cfgpath: the path to the configuration file
 * @main_fn: the first function to run as a thread
 * @arg: an argument to @main_fn
 *
 * Does not return if successful, otherwise return  < 0 if an error.
 */
int runtime_init(const char *cfgpath, thread_fn_t main_fn, void *arg)
{
    pthread_t tid[NCPU];
    int ret, i;

    ret = base_init();
    if (ret) {
        log_err("base library global init failed, ret = %d", ret);
        return ret;
    }

    ret = cfg_load(cfgpath);
    if (ret)
        return ret;

    pthread_barrier_init(&init_barrier, NULL, maxks);

    ret = run_init_handlers("global", global_init_handlers,
                ARRAY_SIZE(global_init_handlers));
    if (ret)
        return ret;

    if (global_init_hook) {
        ret = global_init_hook();
        if (ret) {
            log_err("User-specificed global initializer failed, ret = %d", ret);
            return ret;
        }
    }

    ret = runtime_init_thread();
    BUG_ON(ret);

    log_info("spawning %d kthreads", maxks);
    for (i = 1; i < maxks; i++) {
        ret = pthread_create(&tid[i], NULL, pthread_entry, NULL);
        BUG_ON(ret);
    }

    /* make sure all threads done runtime_init_thread() */
    pthread_barrier_wait(&init_barrier);

    /* establish connection (tids, park_efds, rxq/txq) with hwallocd */
    ret = ioqueues_register_hwallocd();
    if (ret) {
        log_err("couldn't register with iokernel, ret = %d", ret);
        return ret;
    }

    /* point of no return starts here */

    ret = thread_spawn_main(main_fn, arg);
    BUG_ON(ret);

    ret = run_init_handlers("late", late_init_handlers,
                ARRAY_SIZE(late_init_handlers));
    BUG_ON(ret);

    if (late_init_hook) {
        ret = late_init_hook();
        if (ret) {
            log_err("User-specificed late initializer failed, ret = %d", ret);
            return ret;
        }
    }

    sched_start();

    /* never reached unless things are broken */
    BUG();
    return 0;
}

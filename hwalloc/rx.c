/*
 * rx.c - the RXQ sending function (dataplane -> runtimes)
 * it was the receive path for the I/O kernel (network -> runtimes) in shenango
 */

#include <base/log.h>
#include <hwalloc/queue.h>
#include <hwalloc/shm.h>

#include "defs.h"

/**
 * rx_send_to_runtime - enqueues a command to an RXQ for a runtime
 * @p: the runtime's proc structure
 * @hash: the 5-tuple hash for the flow the command is related to
 * @cmd: the command to send
 * @payload: the command payload to send
 *
 * Returns true if the command was enqueued, otherwise a thread is not running
 * and can't be woken or the queue was full.
 */
bool rx_send_to_runtime(struct proc *p, uint32_t hash, uint64_t cmd,
            unsigned long payload)
{
    struct thread *th;

    if (likely(p->active_thread_count > 0)) {
        /* load balance between active threads */
        th = p->active_threads[hash % p->active_thread_count];
    } else if (p->sched_cfg.guaranteed_cores > 0 || get_nr_avail_cores() > 0) {
        th = cores_add_core(p);
        if (unlikely(!th))
            return false;
    } else {
        /* enqueue to the first idle thread, which will be woken next */
        th = list_top(&p->idle_threads, struct thread, idle_link);
        proc_set_overloaded(p);
    }

    return lrpc_send(&th->rxq, cmd, payload);
}

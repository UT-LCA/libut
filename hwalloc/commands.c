/*
 * commands.c - dataplane commands to/from runtimes
 */
#include <base/log.h>
#include <base/lrpc.h>
#include <hwalloc/queue.h>

#include "defs.h"

static int commands_drain_queue(struct thread *t)
{
    int i;

    for (i = 0; i < HWALLOCD_CMD_BURST_SIZE; i++) {
        uint64_t cmd;
        unsigned long payload;

        if (!lrpc_recv(&t->txq, &cmd, &payload))
            break;

        switch (cmd) {
        case TXCMD_PARKED_LAST:
            if (cores_park_kthread(t, false) &&
                t->p->active_thread_count == 0 && payload) {
                t->p->pending_timer = true;
                t->p->deadline_us = microtime() + payload;
            }
            break;
        case TXCMD_PARKED:
            /* notify another kthread if the park was involuntary */
            if (cores_park_kthread(t, false) && payload != 0) {
                rx_send_to_runtime(t->p, t->p->next_thread_rr++, RXCMD_JOIN,
                        payload);
            }
            break;

        default:
            /* kill the runtime? */
            BUG();
        }
    }

    return 0;
}

/*
 * Process a batch of commands from runtimes.
 */
bool commands_recv(void)
{
    int i;
    static unsigned int pos = 0;

    /*
     * Poll each thread in each runtime until all have been polled or we
     * have processed CMD_BURST_SIZE commands.
     */
    for (i = 0; i < nrts; i++) {
        unsigned int idx = (pos + i) % nrts;

        commands_drain_queue(ts[idx]);
    }

    pos++;
    return false;
}

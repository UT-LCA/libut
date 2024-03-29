/*
 * dataplane.c - functions for dataplane operations
 */

#include <base/log.h>
#include <base/lrpc.h>

#include "defs.h"

static struct lrpc_chan_out lrpc_data_to_control;
static struct lrpc_chan_in lrpc_control_to_data;
struct dataplane dp;

/*
 * Add a new client.
 */
static void dp_clients_add_client(struct proc *p)
{
    p->kill = false;
    dp.clients[dp.nr_clients++] = p;

    cores_init_proc(p);
}

void proc_release(struct ref *r)
{
    struct proc *p = container_of(r, struct proc, ref);
    if (!lrpc_send(&lrpc_data_to_control, CONTROLPLANE_REMOVE_CLIENT,
            (unsigned long) p))
        log_err("dp_clients: failed to inform control of client removal");
}

/*
 * Remove a client. Notify control plane once removal is complete so that it
 * can delete its data structures.
 */
static void dp_clients_remove_client(struct proc *p)
{
    int i;

    for (i = 0; i < dp.nr_clients; i++) {
        if (dp.clients[i] == p)
            break;
    }

    if (i == dp.nr_clients) {
        WARN();
        return;
    }

    dp.clients[i] = dp.clients[dp.nr_clients - 1];
    dp.nr_clients--;

    /* TODO: free queued packets/commands? */

    /* release cores assigned to this runtime */
    p->kill = true;
    cores_free_proc(p);
    proc_put(p);
}

/*
 * Process a batch of messages from the control plane.
 */
static void dp_clients_rx_control_lrpcs()
{
    uint64_t cmd;
    unsigned long payload;
    uint16_t n_rx = 0;
    struct proc *p;

    while (lrpc_recv(&lrpc_control_to_data, &cmd, &payload)
            && n_rx < HWALLOCD_CONTROL_BURST_SIZE) {
        p = (struct proc *) payload;

        switch (cmd)
        {
        case DATAPLANE_ADD_CLIENT:
            dp_clients_add_client(p);
            break;
        case DATAPLANE_REMOVE_CLIENT:
            dp_clients_remove_client(p);
            break;
        default:
            log_err("dp_clients: received unrecognized command %lu", cmd);
        }

        n_rx++;
    }
}

/*
 * The main dataplane loop.
 */
void dataplane_loop()
{
    bool work_done;
    uint64_t now, last_time = microtime();

    /* run until quit or killed */
    for (;;) {
        work_done = false;

        /* handle control messages */
        if (!work_done)
            dp_clients_rx_control_lrpcs();

        /* process a batch of commands from runtimes */
        work_done |= commands_recv();

        now = microtime();

        /* adjust core assignments */
        if (now - last_time > CORES_ADJUST_INTERVAL_US) {
            cores_adjust_assignments();
            last_time = now;
        }
    }
}

/*
 * Initialize channels for communicating with the I/O kernel control plane.
 */
int dataplane_init(void)
{
    int ret;

    ret = lrpc_init_in(&lrpc_control_to_data,
            lrpc_control_to_data_params.buffer, CONTROL_DATAPLANE_QUEUE_SIZE,
            lrpc_control_to_data_params.wb);
    if (ret < 0) {
        log_err("dp_clients: initializing LRPC from control plane failed");
        return -1;
    }

    ret = lrpc_init_out(&lrpc_data_to_control,
            lrpc_data_to_control_params.buffer, CONTROL_DATAPLANE_QUEUE_SIZE,
            lrpc_data_to_control_params.wb);
    if (ret < 0) {
        log_err("dp_clients: initializing LRPC to control plane failed");
        return -1;
    }

    dp.nr_clients = 0;

    return 0;
}

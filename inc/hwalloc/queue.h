/*
 * queue.h - shared memory queues between the hwallocd and the runtimes
 */

#pragma once

#include <base/stddef.h>

/*
 * RX queues: HWALLOCD -> RUNTIMES
 * These queues multiplex several different types of requests.
 */
enum {
    RXCMD_JOIN,            /* immediate detach request for a kthread */
    RXCMD_CALL_NR,         /* number of commands */
};


/*
 * TX command queues: RUNTIMES -> HWALLOCD
 * These queues handle a variety of commands.
 */
enum {
    TXCMD_PARKED,          /* hint to iokernel that kthread is parked */
    TXCMD_PARKED_LAST,     /* the last undetached kthread is parking */
    TXCMD_NR,              /* number of commands */
};

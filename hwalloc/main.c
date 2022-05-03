/*
 * main.c - initialization and main loop for the hwallocd
 * (was named iokernel/iok/IOK in Shenango)
 */

#include <base/init.h>
#include <base/log.h>
#include <base/stddef.h>

#include "defs.h"

/* hwallocd subsystem initialization */
static const struct init_handler base_init_handlers[] = {
    /* base */
    BASE_INITIALIZER(base),

    /* hardware resources */
    BASE_INITIALIZER(cores),

    /* control plane */
    BASE_INITIALIZER(control),

    /* data plane */
    BASE_INITIALIZER(dataplane),
};

int main(int argc, char *argv[])
{
    int ret;

    ret = run_init_handlers("hwallocd", base_init_handlers,
            ARRAY_SIZE(base_init_handlers));
    if (ret)
        return ret;

    dataplane_loop();
    return 0;
}

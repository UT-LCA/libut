/*
 * init.c - support for initialization
 */

#include <stdlib.h>

#include <base/init.h>
#include <base/log.h>
#include <base/thread.h>

#include "init_internal.h"

/* base subsystem initialization */
static const struct init_handler base_init_handlers[] = {
    BASE_INITIALIZER(cpu),
    BASE_INITIALIZER(time),
};

/* per-kthread subsystem initialization */
static const struct init_handler thread_init_handlers[] = {
};

int run_init_handlers(const char *phase, const struct init_handler *h,
        int nr)
{
    int i, ret;

    log_debug("entering '%s' init phase", phase);
    for (i = 0; i < nr; i++) {
        log_debug("init -> %s", h[i].name);
        ret = h[i].init();
        if (ret) {
            log_debug("failed, ret = %d", ret);
            return ret;
        }
    }

    return 0;
}

bool base_init_done __aligned(CACHE_LINE_SIZE);

void __weak init_shutdown(int status)
{
    log_info("init: shutting down -> %s",
         status == EXIT_SUCCESS ? "SUCCESS" : "FAILURE");
    exit(status);
}

/**
 * base_init - initializes the base library
 *
 * Call this function before using the library.
 * Returns 0 if successful, otherwise fail.
 */
int base_init(void)
{
    int ret;

    ret = run_init_handlers("base", base_init_handlers,
            ARRAY_SIZE(base_init_handlers));
    if (ret)
        return ret;

    base_init_done = true;
    return 0;
}

/**
 * base_init_thread - prepares a thread for use by the base library
 *
 * Returns 0 if successful, otherwise fail.
 */
int base_init_thread(void)
{
    int ret;

    ret = thread_init_perthread();
    if (ret)
        return ret;
    log_info("thread_init_perthread");

    ret = run_init_handlers("thread", thread_init_handlers,
            ARRAY_SIZE(thread_init_handlers));
    if (ret)
        return ret;

    thread_init_done = true;
    return 0;
}

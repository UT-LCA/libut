/*
 * init.h - support for initialization
 */

#pragma once

#include <base/stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct init_handler {
    const char *name;
    int (*init)(void);
};

#define BASE_INITIALIZER(name) \
    {__cstr(name), &name ## _init}

#define THREAD_INITIALIZER(name) \
    {__cstr(name), &name ## _init_thread}

#define LATE_INITIALIZER(name) \
    {__cstr(name), &name ## _init_late}

#define THREAD_FINALIZER(name) \
    {__cstr(name), &name ## _fini_thread}

extern int run_init_handlers(const char *phase, const struct init_handler *h,
        int nr);

extern int base_init(void);
extern int base_init_thread(void);
extern void init_shutdown(int status) __noreturn;

extern bool base_init_done;
extern __thread bool thread_init_done;

#ifdef __cplusplus
}
#endif

/*
 * thread.h - support for user-level threads
 */

#pragma once

#include <base/types.h>
#include <base/compiler.h>
#include <runtime/preempt.h>

struct thread;
typedef void (*thread_fn_t)(void *arg);
typedef struct thread thread_t;


/*
 * Low-level routines, these are helpful for bindings and synchronization
 * primitives.
 */

extern void thread_park_and_unlock_np(spinlock_t *l);
extern void thread_ready(thread_t *thread);
extern thread_t *thread_create(thread_fn_t fn, void *arg);
extern thread_t *thread_create_with_buf(thread_fn_t fn, void **buf, size_t len);

extern __thread thread_t *__self;

/**
 * thread_self - gets the currently running thread
 */
inline thread_t *thread_self(void)
{
#if defined(__aarch64__) && defined(TLS_LOCAL_EXEC)
    thread_t *tmp;
    unsigned int tmp0; /* transient register */
    asm volatile(
            "mrs  %0,  TPIDR_EL0                       \r\n"
            "ldr  %1,  [%0, #:tprel_lo12: __self]      \r\n"
            : "=r"(tmp0), "=r"(tmp) /* let compiler pick %0 */
            : :);
    return tmp;
#elif defined(__aarch64__)
    thread_t *tmp;
    unsigned int tmp0; /* transient register */
    asm volatile(
            "mrs  %0,  TPIDR_EL0                          \r\n"
            "adrp %1,  _GLOBAL_OFFSET_TABLE_              \r\n"
            "ldr  %1,  [%1, #:gottprel_lo12: __self]      \r\n"
            "ldr  %1,  [%1, %0]                           \r\n"
            : "=r"(tmp0), "=r"(tmp) /* let compiler pick %0 */
            : :);
    return tmp;
#else
    return __self;
#endif
}


/*
 * High-level routines, use this API most of the time.
 */

extern void thread_yield(void);
extern int thread_spawn(thread_fn_t fn, void *arg);
extern void thread_exit(void) __noreturn;

/* main initialization */
typedef int (*initializer_fn_t)(void);

extern int runtime_set_initializers(initializer_fn_t global_fn,
                    initializer_fn_t perthread_fn,
                    initializer_fn_t late_fn);
extern int runtime_init(const char *cfgpath, thread_fn_t main_fn, void *arg);

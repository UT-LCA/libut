/*
 * assert.h - support for assertions
 */

#pragma once

extern void logk_bug(bool fatal, const char *expr,
             const char *file, int line, const char *func);

/* this helper trys to check a run-time assertion at built-time if possible */
#if !defined(__cplusplus)
#define __build_assert_if_constant(cond)                             \
    _Static_assert(__builtin_choose_expr(__builtin_constant_p(cond), \
               (cond), true),                                        \
               "run-time assertion caught at build-time")
#else /* defined(__cplusplus) */
#define __build_assert_if_constant(cond)
#endif

/* these assertions will get compiled out in release builds (fails on false) */
#if DEBUG
#define assert(cond)                               \
        do {                                       \
        __build_assert_if_constant(cond);          \
        if (unlikely(!(cond))) {                   \
            logk_bug(true, __cstr(cond),           \
                 __FILE__, __LINE__, __func__);    \
            __builtin_unreachable();               \
        }                                          \
        } while (0)
#elif !defined(assert) /* DEBUG */
#define assert(cond)                               \
    do {                                           \
        __build_assert_if_constant(cond);          \
        (void)sizeof(cond);                        \
    } while (0)
#endif /* DEBUG */

/**
 * BUG - a fatal code-path that doesn't compile out in release builds
 */
#define BUG()                                      \
    do {                                           \
        logk_bug(true, "false",                    \
             __FILE__, __LINE__, __func__);        \
        __builtin_unreachable();                   \
    } while (0)

/**
 * BUG_ON - a fatal check that doesn't compile out in release builds
 * @condition: the condition to check (fails on true)
 */
#define BUG_ON(cond)                                   \
    do {                                               \
        __build_assert_if_constant(!(cond));           \
        if (unlikely(cond)) {                          \
            logk_bug(true, __cstr(cond),               \
                     __FILE__, __LINE__, __func__);    \
            __builtin_unreachable();                   \
        }                                              \
    } while (0)

/**
 * WARN - a non-fatal code-path that doesn't compile out in release builds
 */
#define WARN()                                              \
    logk_bug(false, "false", __FILE__, __LINE__, __func__);

/**
 * WARN_ON - a non-fatal check that doesn't compile out in release builds
 * @condition: the condition to check (fails on true)
 */
#define WARN_ON(cond)                                  \
    do {                                               \
        __build_assert_if_constant(!(cond));           \
        if (unlikely(cond))                            \
            logk_bug(false, __cstr(cond),              \
                     __FILE__, __LINE__, __func__);    \
    } while (0)

/**
 * WARN_ON_ONCE - a non-fatal check that doesn't compile out in release builds
 * @condition: the condition to check (fails on true)
 */
#define WARN_ON_ONCE(cond)                          \
({                                                  \
    static bool __once;                             \
    __build_assert_if_constant(!(cond));            \
        if (unlikely(!__once && cond)) {            \
        __once = true;                              \
                logk_bug(false, __cstr(cond),       \
             __FILE__, __LINE__, __func__);         \
    }                                               \
})

/**
 * BUILD_ASSERT - assert a build-time condition.
 * @cond: the compile-time condition which must be true.
 *
 * Your compile will fail if the condition isn't true, or can't be evaluated
 * by the compiler.
 */
#if !defined(__cplusplus)
#define BUILD_ASSERT(cond)                              \
    _Static_assert(cond, "build-time condition failed")
#else
#define BUILD_ASSERT(cond)
#endif

/**
 * BUILD_ASSERT_MSG - assert a build-time condition, printing a custom failure
 * message.
 * @cond: the compile-time condition which must be true.
 * @msg: the message to print on failure.
 *
 * Your compile will fail if the condition isn't true, or can't be evaluated
 * by the compiler.
 */
#if !defined(__cplusplus)
#define BUILD_ASSERT_MSG(cond, msg) \
    _Static_assert(cond, msg)
#else
#define BUILD_ASSERT_MSG(cond, msg)
#endif

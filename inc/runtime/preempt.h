/*
 * preempt.h - support for kthread preemption
 */

#pragma once

#include <base/stddef.h>

extern volatile __thread unsigned int preempt_cnt;
extern void preempt(void);

#define PREEMPT_NOT_PENDING    (1 << 31)

/**
 * preempt_disable - disables preemption
 *
 * Can be nested.
 */
static inline void preempt_disable(void)
{
#if defined(__x86_64__)
    asm volatile("addl $1, %%fs:preempt_cnt@tpoff" : : : "memory", "cc");
#elif defined(__aarch64__) && defined(TLS_LOCAL_EXEC)
    unsigned int tmp0, tmp1; /* transient registers */
    asm volatile(
            "mrs  %0,  TPIDR_EL0                       \r\n"
            "ldr  %w1, [%0, #:tprel_lo12: preempt_cnt] \r\n"
            "add  %w1, %w1, #1                         \r\n"
            "str  %w1, [%0, #:tprel_lo12: preempt_cnt] \r\n"
            : "=r"(tmp0), "=r"(tmp1) /* let compiler pick */
            : : "memory");
#elif defined(__aarch64__)
    unsigned int tmp0, tmp1; /* transient registers */
    asm volatile(
            "mrs  %0,  TPIDR_EL0                          \r\n"
            "adrp %1,  _GLOBAL_OFFSET_TABLE_              \r\n"
            "ldr  %1,  [%1, #:gottprel_lo12: preempt_cnt] \r\n"
            "add  %0,  %0, %1                             \r\n"
            "ldr  %w1, [%0]                               \r\n"
            "add  %w1, %w1, #1                            \r\n"
            "str  %w1, [%0]                               \r\n"
            : "=r"(tmp0), "=r"(tmp1) /* let compiler pick */
            : : "memory");
    /* preempt_cnt++; */
#endif
    barrier();
}

/**
 * preempt_enable_nocheck - reenables preemption without checking for conditions
 *
 * Can be nested.
 */
static inline void preempt_enable_nocheck(void)
{
    barrier();
#if defined(__x86_64__)
    asm volatile("subl $1, %%fs:preempt_cnt@tpoff" : : : "memory", "cc");
#elif defined(__aarch64__) && defined(TLS_LOCAL_EXEC)
    unsigned int tmp0, tmp1; /* transient registers */
    asm volatile(
            "mrs  %0,  TPIDR_EL0                       \r\n"
            "ldr  %w1, [%0, #:tprel_lo12: preempt_cnt] \r\n"
            "sub  %w1, %w1, #1                         \r\n"
            "str  %w1, [%0, #:tprel_lo12: preempt_cnt] \r\n"
            : "=r"(tmp0), "=r"(tmp1) /* let compiler pick */
            : : "memory");
#elif defined(__aarch64__)
    unsigned int tmp0, tmp1; /* transient registers */
    asm volatile(
            "mrs  %0,  TPIDR_EL0                          \r\n"
            "adrp %1,  _GLOBAL_OFFSET_TABLE_              \r\n"
            "ldr  %1,  [%1, #:gottprel_lo12: preempt_cnt] \r\n"
            "add  %0,  %0, %1                             \r\n"
            "ldr  %w1, [%0]                               \r\n"
            "sub  %w1, %w1, #1                            \r\n"
            "str  %w1, [%0]                               \r\n"
            : "=r"(tmp0), "=r"(tmp1) /* let compiler pick */
            : : "memory");
    /* preempt_cnt--; */
#endif
}

/**
 * preempt_enable - reenables preemption
 *
 * Can be nested.
 */
static inline void preempt_enable(void)
{
    preempt_enable_nocheck();
    if (unlikely(preempt_cnt == 0))
        preempt();
}

/**
 * preempt_needed - returns true if a preemption event is stuck waiting
 */
static inline bool preempt_needed(void)
{
    return (preempt_cnt & PREEMPT_NOT_PENDING) == 0;
}

/**
 * preempt_enabled - returns true if preemption is enabled
 */
static inline bool preempt_enabled(void)
{
    return (preempt_cnt & ~PREEMPT_NOT_PENDING) == 0;
}

/**
 * assert_preempt_disabled - asserts that preemption is disabled
 */
static inline void assert_preempt_disabled(void)
{
    assert(!preempt_enabled());
}

/**
 * clear_preempt_needed - clear the flag that indicates a preemption request is
 * pending
 */
static inline void clear_preempt_needed(void)
{
#if __cplusplus
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#endif
    preempt_cnt |= PREEMPT_NOT_PENDING;
#if __cplusplus
#pragma GCC diagnostic pop
#endif
}

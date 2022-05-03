/*
 * atomic.h - utilities for atomic memory ops
 *
 * With the exception of *_read and *_write, consider these operations full
 * barriers.
 */

#pragma once

#include <base/types.h>
#include <base/compiler.h>
#include <base/assert.h>

/**
 * mb - a memory barrier
 *
 * Ensures all loads and stores before the barrier complete
 * before all loads and stores after the barrier.
 */
#if defined(__x86_64__)
#define mb() asm volatile("mfence" ::: "memory")
#elif defined(__aarch64__)
#define mb() asm volatile("dmb ish" : : : "memory");
#else
#define mb() barrier()
#endif

/**
 * rmb - a read memory barrier
 *
 * Ensures all loads before the barrier complete before
 * all loads after the barrier.
 */
#if defined(__aarch64__)
#define rmb() asm volatile("dmb ishld" : : : "memory");
#else
#define rmb() barrier()
#endif

/**
 * wmb - a write memory barrier
 *
 * Ensures all stores before the barrier complete before
 * all stores after the barrier.
 */
#if defined(__aarch64__)
#define wmb() asm volatile("dmb ishst" : : : "memory");
#else
#define wmb() barrier()
#endif

/**
 * store_release - store a native value with release fence semantics
 * @p: the pointer to store
 * @v: the value to store
 */
#define store_release(p, v)              \
do {                                     \
    BUILD_ASSERT(type_is_native(*p));    \
    wmb();                               \
    ACCESS_ONCE(*p) = v;                 \
} while (0)

/**
 * load_acquire - load a native value with acquire fence semantics
 * @p: the pointer to load
 */
#define load_acquire(p)                  \
({                                       \
    BUILD_ASSERT(type_is_native(*p));    \
    typeof(*p) __p = ACCESS_ONCE(*p);    \
    rmb();                               \
    __p;                                 \
})

/**
 * load_consume - load a native value with consume fence semantics
 * @p: the pointer to load
 */
#define load_consume(p)                  \
({                                       \
    BUILD_ASSERT(type_is_native(*p));    \
    typeof(*p) __p = ACCESS_ONCE(*p);    \
    rmb();                               \
    __p;                                 \
})

#define ATOMIC_INIT(val) {.cnt = (val)}

static inline int atomic_read(const atomic_t *a)
{
    return *((volatile int *)&a->cnt);
}

static inline void atomic_write(atomic_t *a, int val)
{
    a->cnt = val;
}

static inline int atomic_fetch_and_add(atomic_t *a, int val)
{
    return __sync_fetch_and_add(&a->cnt, val);
}

static inline int atomic_fetch_and_sub(atomic_t *a, int val)
{
    return __sync_fetch_and_add(&a->cnt, val);
}

static inline long atomic_fetch_and_or(atomic_t *a, int val)
{
    return __sync_fetch_and_or(&a->cnt, val);
}

static inline long atomic_fetch_and_xor(atomic_t *a, int val)
{
    return __sync_fetch_and_xor(&a->cnt, val);
}

static inline long atomic_fetch_and_and(atomic_t *a, int val)
{
    return __sync_fetch_and_and(&a->cnt, val);
}

static inline long atomic_fetch_and_nand(atomic_t *a, int val)
{
    return __sync_fetch_and_nand(&a->cnt, val);
}

static inline int atomic_add_and_fetch(atomic_t *a, int val)
{
    return __sync_add_and_fetch(&a->cnt, val);
}

static inline int atomic_sub_and_fetch(atomic_t *a, int val)
{
    return __sync_sub_and_fetch(&a->cnt, val);
}

static inline void atomic_inc(atomic_t *a)
{
    atomic_fetch_and_add(a, 1);
}

static inline void atomic_dec(atomic_t *a)
{
    atomic_sub_and_fetch(a, 1);
}

static inline bool atomic_dec_and_test(atomic_t *a)
{
    return (atomic_sub_and_fetch(a, 1) == 0);
}

static inline bool atomic_cmpxchg(atomic_t *a, int oldv, int newv)
{
    return __sync_bool_compare_and_swap(&a->cnt, oldv, newv);
}

static inline int atomic_cmpxchg_val(atomic_t *a, int oldv, int newv)
{
    return __sync_val_compare_and_swap(&a->cnt, oldv, newv);
}

static inline long atomic64_read(const atomic64_t *a)
{
    return *((volatile long *)&a->cnt);
}

static inline void atomic64_write(atomic64_t *a, long val)
{
    a->cnt = val;
}

static inline long atomic64_fetch_and_add(atomic64_t *a, long val)
{
    return __sync_fetch_and_add(&a->cnt, val);
}

static inline long atomic64_fetch_and_sub(atomic64_t *a, long val)
{
    return __sync_fetch_and_sub(&a->cnt, val);
}

static inline long atomic64_fetch_and_or(atomic64_t *a, long val)
{
    return __sync_fetch_and_or(&a->cnt, val);
}

static inline long atomic64_fetch_and_xor(atomic64_t *a, long val)
{
    return __sync_fetch_and_xor(&a->cnt, val);
}

static inline long atomic64_fetch_and_nand(atomic64_t *a, long val)
{
    return __sync_fetch_and_nand(&a->cnt, val);
}

static inline long atomic64_fetch_and_and(atomic64_t *a, long val)
{
    return __sync_fetch_and_and(&a->cnt, val);
}

static inline long atomic64_add_and_fetch(atomic64_t *a, long val)
{
    return __sync_add_and_fetch(&a->cnt, val);
}

static inline long atomic64_sub_and_fetch(atomic64_t *a, long val)
{
    return __sync_sub_and_fetch(&a->cnt, val);
}

static inline void atomic64_inc(atomic64_t *a)
{
    atomic64_fetch_and_add(a, 1);
}

static inline void atomic64_dec(atomic64_t *a)
{
    atomic64_sub_and_fetch(a, 1);
}

static inline bool atomic64_dec_and_test(atomic64_t *a)
{
    return (atomic64_sub_and_fetch(a, 1) == 0);
}

static inline bool atomic64_cmpxchg(atomic64_t *a, long oldv, long newv)
{
    return __sync_bool_compare_and_swap(&a->cnt, oldv, newv);
}

static inline long atomic64_cmpxchg_val(atomic64_t *a, long oldv, long newv)
{
    return __sync_val_compare_and_swap(&a->cnt, oldv, newv);
}

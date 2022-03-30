/*
 * compiler.h - useful compiler hints, intrinsics, and attributes
 */

#pragma once

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#define unreachable() __builtin_unreachable()

/* variable attributes */
#define __packed __attribute__((packed))
#define __notused __attribute__((unused))
#define __used __attribute__((used))
#define __aligned(x) __attribute__((aligned(x)))

/* function attributes */
#define __noinline __attribute__((noinline))
#define __noreturn __attribute__((noreturn))
#define __must_use_return __attribute__((warn_unused_result))
#define __pure __attribute__((pure))
#define __weak __attribute__((weak))
#define __malloc __attribute__((malloc))
#define __assume_aligned(x) __attribute__((assume_aligned(x)))

#define barrier() asm volatile("" ::: "memory")

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#define type_is_native(t)          \
    (sizeof(t) == sizeof(char)  || \
     sizeof(t) == sizeof(short) || \
     sizeof(t) == sizeof(int)   || \
     sizeof(t) == sizeof(long))

/*
 * These attributes are defined only with the sparse checker tool.
 */
#define __rcu
#define __perthread
#define __force

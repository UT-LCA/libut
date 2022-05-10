/*
 * ops.h - useful x86_64 instructions
 */

#pragma once

#include <base/types.h>

static inline void cpu_relax(void)
{
#if defined(__x86_64__)
    asm volatile("pause");
#elif defined(__aarch64__) && defined(RELAX_IS_ISB)
    asm volatile("isb" : : : "memory");
#elif defined(__aarch64__)
    asm volatile("yield" : : : "memory");
#endif
}

static inline void cpu_serialize(void)
{
#if defined(__x86_64__)
    asm volatile("cpuid" : : : "%rax", "%rbx", "%rcx", "%rdx");
#elif defined(__aarch64__)
    asm volatile("dsb SY");
#endif
}

static inline uint64_t libut_rdtsc(void)
{
#if defined(__x86_64__)
    uint32_t a, d;
    asm volatile("rdtsc" : "=a" (a), "=d" (d));
    return ((uint64_t)a) | (((uint64_t)d) << 32);
#elif defined(__aarch64__)
    uint64_t cntvct;
    asm volatile("mrs %0, CNTVCT_EL0" : "=r"(cntvct));
    return cntvct;
#endif
}

static inline uint64_t libut_rdtscp(uint32_t *auxp)
{
#if defined(__x86_64__)
    uint32_t a, d, c;
    asm volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
    if (auxp)
        *auxp = c;
    return ((uint64_t)a) | (((uint64_t)d) << 32);
#elif defined(__aarch64__)
    uint64_t cntvct;
    asm volatile("dmb ishld" : : : "memory");
    asm volatile("mrs %0, CNTVCT_EL0" : "=r"(cntvct));
    if (auxp)
        *auxp = 0;
    return cntvct;
#endif
}

static inline uint64_t __mm_crc32_u64(uint64_t crc, uint64_t val)
{
#if defined(__x86_64__)
    asm("crc32q %1, %0" : "+r" (crc) : "rm" (val));
#elif defined(__aarch64__)
    asm("crc32x %w0, %w0, %1" : "+r" (crc) : "r" (val));
#endif
    return crc;
}

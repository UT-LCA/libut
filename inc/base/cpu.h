/*
 * cpu.h - determine CPU specification/topology
 */

#pragma once

#include <base/stddef.h>
#include <base/limits.h>
#include <base/bitmap.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int cpu_count; /* the number of available CPUs */
extern int l2_count; /* the number of available L2 clusters */
extern int numa_count; /* the number of NUMA nodes */

struct cpu_info {
    DEFINE_BITMAP(thread_siblings_mask, NCPU);
    DEFINE_BITMAP(core_siblings_mask, NCPU);
    DEFINE_BITMAP(l2_siblings_mask, NCPU);
    int package;
    int cluster;
};

extern struct cpu_info cpu_info_tbl[NCPU];

#ifdef __cplusplus
}
#endif

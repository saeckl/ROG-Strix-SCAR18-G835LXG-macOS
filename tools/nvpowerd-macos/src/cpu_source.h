/* SPDX-License-Identifier: MIT */
#ifndef NV_CPU_SOURCE_H
#define NV_CPU_SOURCE_H
#include "nvpowerd.h"
#include <stdio.h>
typedef struct {
    bool started,failed;
    int pid,fd;
    unsigned char *frame;
    size_t used;
    uint64_t seq,last_ns,poll_ns;
    uint32_t mw,interval_ms;
    FILE *trace;
} NvCpuSource;
int nv_cpu_energy(double joules,double elapsed_ns,uint32_t *mw,uint32_t *interval_ms);
int nv_cpu_start(NvCpuSource *,FILE *);
int nv_cpu_read(NvCpuSource *,NvCpuSample *);
int nv_cpu_stop(NvCpuSource *);
#endif

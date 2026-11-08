/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "cpu_source.h"
#include <time.h>
#include <stdlib.h>
int main(int argc,char **argv){
    unsigned loops=argc>1?(unsigned)atoi(argv[1])*10u:40u,need=argc>1?1000000u:3u;
    NvCpuSource s={0};int e=nv_cpu_start(&s,stdout);unsigned samples=0;uint64_t seq=0;
    if(e){fprintf(stderr,"CPU sampler unavailable: %d\n",e);return 1;}
    for(unsigned i=0;i<loops && samples<need;i++){
        NvCpuSample sample={0};
        if(!nv_cpu_read(&s,&sample) && sample.valid && sample.seq!=seq){seq=sample.seq;samples++;fprintf(stdout,"CPU package=%u mW age=%u ms interval=%u ms (read-only)\n",sample.mw,sample.age_ms,sample.interval_ms);}
        struct timespec delay={0,100000000};(void)nanosleep(&delay,NULL);
    }
    if(argc>1)fprintf(stdout,"fresh samples: %u of ~%u\n",samples,loops);
    int cleanup=nv_cpu_stop(&s);
    if((argc<2&&samples<3)||cleanup){fprintf(stderr,"CPU source not qualified: fresh=%u cleanup=%d; boost must stay disabled\n",samples,cleanup);return 1;}
    return 0;
}

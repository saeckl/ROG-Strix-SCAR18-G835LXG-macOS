/* SPDX-License-Identifier: MIT */
#include "nvpowerd.h"
#include "cpu_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"regulator FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void load(const char *dir,const char *name,uint8_t *b){char p[1024];snprintf(p,sizeof(p),"%s/%s.bin",dir,name);FILE*f=fopen(p,"rb");CHECK(f);CHECK(fread(b,1,NVPCF_SIZE,f)==NVPCF_SIZE);CHECK(fclose(f)==0);}
int main(int argc,char **argv){
    CHECK(argc==2);uint8_t dyn[NVPCF_SIZE];char error[256];NvPowerProfile p;NvDecision d;NvRegulator r={0};
    load(argv[1],"balanced",dyn);CHECK(!nv_profile(dyn,sizeof(dyn),&p,error,sizeof(error)) && !p.boost);
    CHECK(!nv_regulate(&r,&p,NULL,175000,0,true,&d) && d.target_mw==150000);
    load(argv[1],"quiet",dyn);CHECK(!nv_profile(dyn,sizeof(dyn),&p,error,sizeof(error)) && !p.boost);
    CHECK(!nv_regulate(&r,&p,NULL,150000,0,true,&d) && d.target_mw==95000);
    load(argv[1],"performance",dyn);CHECK(!nv_profile(dyn,sizeof(dyn),&p,error,sizeof(error)) && p.boost);
    CHECK(p.total_mw==270000 && p.min_mw==140000 && p.max_mw==175000);
    CHECK(!nv_regulate(&r,&p,NULL,175000,175000,true,&d) && d.target_mw==95000 && d.mode==NV_CPU_MISSING);
    NvCpuSample sample={true,10000,0,100,1};
    CHECK(!nv_regulate(&r,&p,&sample,175000,0,true,&d) && d.target_mw==150000 && d.mode==NV_WARMUP);
    CHECK(!nv_regulate(&r,&p,&sample,175000,150000,true,&d) && d.target_mw==150000); /* no duplicate-sample ramp */
    sample.seq++;CHECK(!nv_regulate(&r,&p,&sample,175000,150000,true,&d) && d.target_mw==175000);
    /* Original binary arithmetic oracle: stable package power, previous 175 W. */
    const uint32_t cpu[]={10000,80000,95000,100000,120000,130000};
    const uint32_t gpu[]={175000,175000,175000,170000,150000,140000};
    for(unsigned i=0;i<6;i++){
        r=(NvRegulator){0};sample=(NvCpuSample){true,cpu[i],0,100,10};
        CHECK(!nv_regulate(&r,&p,&sample,175000,175000,true,&d));sample.seq++;
        CHECK(!nv_regulate(&r,&p,&sample,175000,175000,true,&d) && d.target_mw==gpu[i]);
    }
    /* CPU load step must lower the ceiling immediately, despite EMWA lag. */
    r=(NvRegulator){0};sample=(NvCpuSample){true,10000,0,100,100};
    CHECK(!nv_regulate(&r,&p,&sample,175000,175000,true,&d));
    sample.mw=120000;sample.seq++;CHECK(!nv_regulate(&r,&p,&sample,175000,175000,true,&d) && d.target_mw==150000);
    sample.mw=130001;sample.seq++;CHECK(!nv_regulate(&r,&p,&sample,175000,175000,true,&d) && d.target_mw==95000 && d.mode==NV_BUDGET_EXHAUSTED);
    for(unsigned issue=0;issue<6;issue++){
        sample=(NvCpuSample){true,10000,0,100,1000+issue};
        if(issue==0)sample.age_ms=351;
        if(issue==1)sample.interval_ms=1000;
        if(issue==2)sample.valid=false;
        if(issue==3)sample.mw=0;
        if(issue==4)sample.mw=500001;
        if(issue==5)sample.seq=0;
        CHECK(!nv_regulate(&r,&p,&sample,175000,175000,true,&d) && d.target_mw==95000 && !r.primed);
    }
    /* Grace: a primed controller holds its last target for 10 missing ticks, then 95 W. */
    r=(NvRegulator){0};sample=(NvCpuSample){true,20000,0,100,500};
    CHECK(!nv_regulate(&r,&p,&sample,175000,150000,true,&d));sample.seq++;
    CHECK(!nv_regulate(&r,&p,&sample,175000,150000,true,&d));uint32_t held=d.target_mw;CHECK(held>=150000);
    {NvCpuSample gone={0};
     for(unsigned k=0;k<CPU_GRACE_TICKS;k++){CHECK(!nv_regulate(&r,&p,&gone,175000,held,true,&d) && d.target_mw==held && d.mode==NV_CPU_MISSING && r.primed);}
     CHECK(!nv_regulate(&r,&p,&gone,175000,held,true,&d) && d.target_mw==95000 && !r.primed);
     /* a fresh sample resets the grace counter */
     r=(NvRegulator){0};CHECK(!nv_regulate(&r,&p,&sample,175000,150000,true,&d));sample.seq++;CHECK(!nv_regulate(&r,&p,&sample,175000,150000,true,&d));
     for(unsigned k=0;k<5;k++)CHECK(!nv_regulate(&r,&p,&gone,175000,held,true,&d) && d.target_mw==r.hold_mw);
     sample.seq++;CHECK(!nv_regulate(&r,&p,&sample,175000,held,true,&d) && r.missing==0);
     /* holding never exceeds the profile cap after a profile drop */
     NvPowerProfile low=p;low.max_mw=150000;CHECK(!nv_regulate(&r,&p,&gone,175000,held,true,&d));
     CHECK(!nv_regulate(&r,&low,&gone,175000,held,true,&d) && d.target_mw<=150000);}
    sample=(NvCpuSample){true,10000,0,100,10};CHECK(!nv_regulate(&r,&p,&sample,175000,175000,false,&d) && d.target_mw==95000);
    CHECK(nv_regulate(&r,&p,&sample,200000,0,true,&d)!=0);
    uint8_t bad[NVPCF_SIZE];memcpy(bad,dyn,sizeof(bad));nv_put32(bad,24,125000);
    CHECK(nv_profile(bad,sizeof(bad),&p,error,sizeof(error))!=0);
    CHECK(!nv_profile(dyn,sizeof(dyn),&p,error,sizeof(error)));
    /* Sweep both requested limits and all feasible CPU headroom values. */
    for(uint32_t request=150000;request<=175000;request+=25000){
        uint32_t current=150000;r=(NvRegulator){0};
        for(unsigned i=0;i<1000;i++){
            sample=(NvCpuSample){true,1000+(i*7919u)%129001u,0,100,10000+i};
            CHECK(!nv_regulate(&r,&p,&sample,request,current,true,&d));
            CHECK(d.target_mw>=140000 && d.target_mw<=request && d.target_mw<=175000);
            CHECK(d.target_mw+sample.mw<=270000 && d.target_mw%500==0);current=d.target_mw;
        }
    }
    uint32_t mw,interval;CHECK(!nv_cpu_energy(8.0,100000000,&mw,&interval) && mw==80000 && interval==100);
    CHECK(!nv_cpu_energy(4.0,203000000,&mw,&interval) && interval==203 && mw==19705); /* 200 ms powermetrics phase */
    CHECK(nv_cpu_energy(4.0,251000000,&mw,&interval));
    CHECK(nv_cpu_energy(NAN,1e8,&mw,&interval));CHECK(nv_cpu_energy(0,1e8,&mw,&interval));
    CHECK(nv_cpu_energy(1,1e9,&mw,&interval));CHECK(nv_cpu_energy(INFINITY,1e8,&mw,&interval));
    CHECK(nv_cpu_energy(51,1e8,&mw,&interval));
    printf("PASS: %u regulator/CPU-energy checks (fixtures, original-math cases, headroom sweep)\n",checks);return 0;
}

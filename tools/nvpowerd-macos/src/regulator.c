/* SPDX-License-Identifier: MIT
 * Independently written, restricted SCAR18 AC feedback controller.
 * Arithmetic basis: original 0x4ddc80/0x4dddf0/0x4dd860, 595.99.02.
 * Raw-power ceiling, warmup and stale-data gates are deliberate additions. */
#include "nvpowerd.h"
#include <stdio.h>
#include <string.h>
static int bad(char *e,size_t n,const char *why){snprintf(e,n,"%s",why);return -1;}
int nv_profile(const uint8_t *p,size_t n,NvPowerProfile *out,char *e,size_t en){
    if(n!=NVPCF_SIZE || nv_get32(p,16)!=3)return bad(e,en,"invalid NVPCF dynamic reply");
    int32_t ctgp=(int32_t)nv_get32(p,20),tpp=(int32_t)nv_get32(p,24);
    int32_t hi=(int32_t)nv_get32(p,28),lo=(int32_t)nv_get32(p,32);
    if(lo!=-10000 || p[73]!=0)return bad(e,en,"unsupported NVPCF lower offset/DC controller");
    if(ctgp==0 && tpp==35000 && hi==0 && p[72]==0)
        *out=(NvPowerProfile){95000,130000,95000,95000,false};
    else if(ctgp==55000 && tpp==45000 && hi==0 && p[72]==0)
        *out=(NvPowerProfile){150000,195000,150000,150000,false};
    else if(ctgp==55000 && tpp==120000 && hi==25000 && p[72]==1)
        *out=(NvPowerProfile){150000,270000,140000,175000,true};
    else return bad(e,en,"NVPCF budget/enable tuple outside captured OEM profiles");
    return 0;
}
static uint32_t minimum(uint32_t a,uint32_t b){return a<b?a:b;}
int nv_regulate(NvRegulator *r,const NvPowerProfile *p,const NvCpuSample *cpu,
                uint32_t request,uint32_t current,bool mains,NvDecision *d){
    if(request!=150000 && request!=175000)return -1;
    *d=(NvDecision){minimum(request,p->base_mw),0,NV_FIXED};
    if(!mains || !p->boost){memset(r,0,sizeof(*r));if(!mains)d->target_mw=BASE_MW;return 0;}
    if(!cpu || !cpu->valid || !cpu->seq || !cpu->mw || cpu->mw>500000 ||
       cpu->age_ms>350 || cpu->interval_ms<75 || cpu->interval_ms>250){
        /* macOS powermetrics alternates between ~100 and ~200 ms intervals
           (measured on SCAR18, idle and GPU load); both are package averages. */
        memset(r,0,sizeof(*r));d->mode=NV_CPU_MISSING;d->target_mw=BASE_MW;return 0;
    }
    /* With no CPU actuator, do not claim to satisfy an impossible budget. */
    if(cpu->mw>p->total_mw-p->min_mw){
        memset(r,0,sizeof(*r));d->mode=NV_BUDGET_EXHAUSTED;d->target_mw=BASE_MW;return 0;
    }
    uint32_t ceiling=minimum(request,minimum(p->max_mw,((p->total_mw-cpu->mw)/500)*500));
    if(!r->primed || r->profile_cap!=p->max_mw){
        *r=(NvRegulator){true,cpu->mw,p->max_mw,cpu->seq,0};
        d->mode=NV_WARMUP;d->filtered_mw=cpu->mw;
        d->target_mw=minimum(d->target_mw,ceiling);return 0;
    }
    d->filtered_mw=r->filtered_mw;d->mode=NV_FEEDBACK;
    /* The same sample must not advance the EMWA or the setter accumulator. */
    bool fresh=cpu->seq!=r->seq;
    if(fresh){
        int64_t diff=(int64_t)cpu->mw-r->filtered_mw;
        int64_t step=(diff<0?-diff:diff)*24/256; /* weight=232 -> 24/256 */
        r->filtered_mw=(uint32_t)((int64_t)r->filtered_mw+(diff<0?-step:step));r->seq=cpu->seq;
    }
    uint32_t previous=current>=p->min_mw && current<=p->max_mw?current:p->base_mw;
    int64_t error=(int64_t)p->total_mw-r->filtered_mw-previous;
    /* Captured gains: increase Q12=3686, decrease Q12=4096 (exact unity). */
    int64_t candidate=(int64_t)previous+(error>0?(error*3686+2048)/4096:error);
    if(candidate<p->min_mw)candidate=p->min_mw;
    if(candidate>p->max_mw)candidate=p->max_mw;
    uint32_t target=minimum(ceiling,(uint32_t)((candidate+250)/500)*500);
    /* Original setter accumulates deviations until +/-1000 mW. Reductions
       required by raw CPU headroom bypass that hysteresis immediately. */
    if(fresh)r->delta_sum+=(int64_t)target-previous;
    if(target>previous && r->delta_sum<1000)target=previous;
    if(target<previous && previous<=ceiling && r->delta_sum>-1000)target=previous;
    if(target!=previous)r->delta_sum=0;
    d->target_mw=minimum(target,ceiling);d->filtered_mw=r->filtered_mw;return 0;
}

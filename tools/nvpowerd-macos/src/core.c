/* SPDX-License-Identifier: MIT */
#include "nvpowerd.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
uint32_t nv_get32(const void *b,size_t off){const uint8_t *p=(const uint8_t*)b+off;return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
void nv_put32(void *b,size_t off,uint32_t v){uint8_t *p=(uint8_t*)b+off;for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static int err(char *e,size_t n,const char *s){snprintf(e,n,"%s",s);return -1;}
static int fail(NvSession *s,const char *fmt,...){va_list a;va_start(a,fmt);vsnprintf(s->error,sizeof(s->error),fmt,a);va_end(a);return -1;}
int nv_policy_check(const uint8_t *p,size_t n,char *e,size_t en){return nv_policy_check_tag(p,n,0x6060,e,en);}
int nv_policy_check_tag(const uint8_t *p,size_t n,uint32_t tag,char *e,size_t en){
    if(n!=POLICY_SIZE)return err(e,en,"PMGR policy size differs from captured ABI");
    /* 0x4db1a0: mask +4, descriptor +8 +36*i, type byte +2. Limit offsets
       and profile fingerprint are trace-derived, NOT vendor field names. */
    if(nv_get32(p,0)!=0xff || nv_get32(p,4)!=1 || p[10]!=0)
        return err(e,en,"unsupported PMGR policy header/mask/type (expected captured slot 0)");
    if(nv_get32(p,16)!=BASE_MW || nv_get32(p,20)!=175000u)
        return err(e,en,"OEM PMGR profile fingerprint differs from SCAR18 capture (95000/175000)");
    /* Do not gate on unexplained power/offset/default words: they may change
       after a setter or firmware profile event. Check the descriptor tag only. */
    if(nv_get32(p,8)!=tag)
        return err(e,en,"PMGR descriptor tag differs from captured ABI/profile");
    return 0;
}
int nv_budget(const uint8_t *p,size_t n,uint32_t requested,uint32_t *target,char *e,size_t en){
    NvPowerProfile profile;
    if(requested!=150000 && requested!=175000)return err(e,en,"only 150/175 W requests are supported");
    if(nv_profile(p,n,&profile,e,en))return -1;
    *target=requested<profile.max_mw?requested:profile.max_mw;return 0;
}
void nv_build_set(uint8_t out[SET_SIZE],uint32_t mw){
    memset(out,0,SET_SIZE);nv_put32(out,4,1);nv_put32(out,12,mw);out[16]=0xf7;
}
static int esc(NvSession *s,uint32_t nr,void *p,size_t n){
    int r=s->io.escape(s->io.opaque,nr,p,n);
    return r?fail(s,"escape 0x%x transport error %d",nr,r):0;
}
int nv_control(NvSession *s,uint32_t obj,uint32_t cmd,void *p,size_t n){
    if(n>UINT32_MAX)return fail(s,"oversized control");
    NvControl c={s->root,obj,cmd,0,(uint64_t)(uintptr_t)p,(uint32_t)n,NV_INVALID};
    if(esc(s,0x2a,&c,sizeof(c)))return -1;
    return c.status?fail(s,"control 0x%08x RM status 0x%x",cmd,c.status):0;
}
static int alloc(NvSession *s,uint32_t parent,uint32_t *handle,uint32_t cls,void *params){
    NvAlloc a={0};a.root=s->root;a.parent=parent;a.object=*handle;a.cls=cls;
    a.params=(uint64_t)(uintptr_t)params;a.status=NV_INVALID;
    /* Legacy RM infers allocation size from class; mirrors measured NVOS64. */
    if(esc(s,0x2b,&a,sizeof(a)))return -1;
    if(a.status || !a.object)return fail(s,"alloc class 0x%x RM status 0x%x / empty handle",cls,a.status);
    *handle=a.object;return 0;
}
/* Offsets from NVIDIA's public headers (tags 595.99.02 / 620.70.06, clang
   record layouts). 620 inserts nvpcfFunc at +16 and drops the two
   dcTsp*TimescaleLimitmA words; everything after +56 moves by -4. */
void nv_nvpcf_from620(const uint8_t in[NVPCF_SIZE_620],uint8_t out[NVPCF_SIZE]){
    memset(out,0,NVPCF_SIZE);
    memcpy(out,in,16);              /* supportedFuncs,gpuId,tpp,ratedTgp */
    memcpy(out+16,in+20,4);         /* subFunc */
    memcpy(out+20,in+24,36);        /* ctgp..minOutputBatt, dcRosReserveOverride */
    memcpy(out+60,in+60,4);         /* dcTspLongTimescaleLimitOverridemA */
    memcpy(out+68,in+64,4);         /* dcTspShortTimescaleLimitOverridemA */
    memcpy(out+72,in+68,28);        /* bEnableForAC .. sysPwrIndex */
    memcpy(out+100,in+96,784);      /* sysPwrGetInfo .. cpuTdpControlType */
}
static int nvpcf(NvSession *s,unsigned sub,uint8_t b[NVPCF_SIZE]){
    /* nvidia-powerd sends GET_DYNAMIC (3) with the static reply as input: RM
       copies .version into the ACPI request and rejects a reply whose header
       version differs (NV_ERR_INVALID_DATA). Captured 595 requests carry 37. */
    uint8_t req[NVPCF_SIZE]={0};
    if(sub==3){if(!s->static_valid)return fail(s,"NVPCF dynamic read before verified static config");memcpy(req,s->static_conf,NVPCF_SIZE);}
    nv_put32(req,4,s->gpu);nv_put32(req,16,sub);
    if(s->nvpcf_abi==620){
        /* nvpcfFunc 0 (not NVPCF_3X), inOutData NULL: the 2x path of the 620 handler. */
        uint8_t w[NVPCF_SIZE_620];nv_nvpcf_to620(req,w);
        if(nv_control(s,s->root,C_NVPCF,w,sizeof(w)))return -1;
        if(nv_get32(w,4)!=s->gpu || nv_get32(w,20)!=sub || nv_get32(w,16) || (nv_get32(w,880)|nv_get32(w,884)) || w[888]|w[889])
            return fail(s,"NVPCF 620 reply changed request fields");
        nv_nvpcf_from620(w,b);return 0;
    }
    memcpy(b,req,NVPCF_SIZE);
    return nv_control(s,s->root,C_NVPCF,b,NVPCF_SIZE);
}
/* 0x4db470: preferred byte +0x28, fallback +0x2a, mask +4,
   type +0x5e + 0xe4*i; hardware floor/base/max/reference from the loads. */
int nv_policy_info(const uint8_t *p,size_t n,NvPolicyInfo *out,char *e,size_t en){
    if(n!=POLICY_INFO_SIZE)return err(e,en,"invalid PMGR info size");
    unsigned i=p[0x28]!=0xff?p[0x28]:p[0x2a];
    if(i>=32 || !(nv_get32(p,4)&(1u<<i)))return err(e,en,"no valid PMGR info selector/mask");
    size_t o=(size_t)i*0xe4;
    if(o+0x104>n || p[o+0x5e]!=0)return err(e,en,"unsupported PMGR info type/range");
    NvPolicyInfo v={i,nv_get32(p,o+0x60),nv_get32(p,o+0x64),nv_get32(p,o+0x68),nv_get32(p,o+0x100)};
    if(v.hardware_min_mw!=5000 || v.base_mw!=BASE_MW || v.max_mw!=175000 || v.reference_mw!=150000)
        return err(e,en,"PMGR info hardware-min/base/max/reference differ from measured SCAR18");
    *out=v;return 0;
}
/* 620 (GSP table size 20512): selector +0x98/+0x9a, entry base 116+260*i,
   type +0x5e, floor +0x60, base +0x64, max +0x68, reference +0x10c (dumped
   on this SCAR18 with NullMoth 2.8; same values as the 595 capture). */
int nv_policy_info_620(const uint8_t *p,size_t n,NvPolicyInfo *out,char *e,size_t en){
    if(n!=POLICY_INFO_SIZE_620)return err(e,en,"invalid 620 PMGR info size");
    unsigned i=p[0x98]!=0xff?p[0x98]:p[0x9a];
    if(i>=32 || !(nv_get32(p,4)&(1u<<i)))return err(e,en,"no valid 620 PMGR info selector/mask");
    size_t o=116+(size_t)i*260;
    if(o+0x110>n || p[o+0x5e]!=0)return err(e,en,"unsupported 620 PMGR info type/range");
    NvPolicyInfo v={i,nv_get32(p,o+0x60),nv_get32(p,o+0x64),nv_get32(p,o+0x68),nv_get32(p,o+0x10c)};
    if(v.hardware_min_mw!=5000 || v.base_mw!=BASE_MW || v.max_mw!=175000 || v.reference_mw!=150000)
        return err(e,en,"620 PMGR info hardware-min/base/max/reference differ from measured SCAR18");
    *out=v;return 0;
}
/* 620 policy: word 0, E255 mask +4..+35, 64 entries of 36 bytes from +36.
   Canonical 595: word 0, E32 mask +4, 32 entries from +8. Refuse anything
   that does not fit the smaller layout (mask bits or entries beyond 32). */
int nv_policy_from620(const uint8_t in[POLICY_SIZE_620],uint8_t out[POLICY_SIZE],char *e,size_t en){
    for(size_t k=8;k<36;k++)if(in[k])return err(e,en,"620 PMGR policy mask uses entries beyond 32");
    for(size_t k=36+32*36;k<POLICY_SIZE_620;k++)if(in[k])return err(e,en,"620 PMGR policy entries beyond 32 populated");
    memset(out,0,POLICY_SIZE);memcpy(out,in,8);memcpy(out+8,in+36,32*36);return 0;
}
/* 620 setter, from the GSP 620.70.06 handler of 0x2080e634 (va 0x165afe4):
   mask imported from +4 (E255), entries at +0x28 + 12*i with limit mW at
   +0x2c and an action byte at +0x30 that must be 0xf7 (or 0xf4), otherwise
   NV_ERR_INVALID_ARGUMENT. 4 + 36 + 64*12 = 808. Slot 0 as in the 595 capture. */
void nv_build_set_620(uint8_t out[SET_SIZE_620],uint32_t mw){
    memset(out,0,SET_SIZE_620);nv_put32(out,4,1);nv_put32(out,0x2c,mw);out[0x30]=0xf7;
}
void nv_nvpcf_to620(const uint8_t in[NVPCF_SIZE],uint8_t out[NVPCF_SIZE_620]){
    memset(out,0,NVPCF_SIZE_620);memcpy(out,in,16);memcpy(out+20,in+16,4);memcpy(out+24,in+20,36);
    memcpy(out+60,in+60,4);memcpy(out+64,in+68,4);memcpy(out+68,in+72,28);memcpy(out+96,in+100,784);
}
static uint8_t info_buf[POLICY_INFO_SIZE_620];
static int query_info(NvSession *s){
    size_t n=s->nvpcf_abi==620?POLICY_INFO_SIZE_620:POLICY_INFO_SIZE;
    memset(info_buf,0,n);
    if(nv_control(s,s->subdevice,C_POLICY_INFO,info_buf,n))return -1;
    return s->nvpcf_abi==620?nv_policy_info_620(info_buf,n,&s->policy,s->error,sizeof(s->error))
                            :nv_policy_info(info_buf,n,&s->policy,s->error,sizeof(s->error));
}
static int refresh_static(NvSession *s){
    uint8_t conf[NVPCF_SIZE];
    if(nvpcf(s,4,conf))return -1;
    if(conf[74]!=37 || (unsigned)(conf[76]|conf[77]<<8)!=100 ||
       (unsigned)(conf[78]|conf[79]<<8)!=1 || conf[80]!=0 || conf[81]!=232 ||
       conf[84]!=1 || (unsigned)(conf[86]|conf[87]<<8)!=3686 ||
       (unsigned)(conf[88]|conf[89]<<8)!=4096)
        return fail(s,"NVPCF controller config differs from captured 37/100ms/EMWA232/Q12 gains");
    memcpy(s->static_conf,conf,NVPCF_SIZE);s->static_valid=true;
    return 0;
}
static const uint32_t event_ids[EVENT_COUNT]={118,181,177,33};
static int notify(NvSession *s,unsigned i,uint32_t action){
    uint8_t b[20]={0};nv_put32(b,0,event_ids[i]);nv_put32(b,4,action);
    return nv_control(s,s->subdevice,C_NOTIFY,b,sizeof(b));
}
static int device_esc(NvSession *s,uint32_t nr,void *p,size_t n){
    int r=s->io.device_escape(s->io.opaque,nr,p,n);
    return r?fail(s,"device escape 0x%x transport error %d",nr,r):0;
}
static int start_events(NvSession *s){
    NvOsEvent p={s->root,s->device,s->event_fd,NV_INVALID};
    /* Even an uncertain reply requires a FREE_OS_EVENT attempt during cleanup. */
    s->event_os=true;
    if(device_esc(s,0xce,&p,sizeof(p)))return -1;
    if(p.status)return fail(s,"ALLOC_OS_EVENT status 0x%x",p.status);
    for(unsigned i=0;i<EVENT_COUNT;i++){
        NvEventParams ep={s->root,0,0x79,event_ids[i],s->event_fd};
        uint32_t h=0x60000010u+i;
        if(alloc(s,s->subdevice,&h,0x79,&ep))return -1;
        s->events[i]=h;
        s->notification_attempted[i]=true;
        if(notify(s,i,2))return -1;
    }
    return 0;
}

static int begin(NvSession *s,NvTransport t,const char *version,bool open_only){
    memset(s,0,sizeof(*s));s->io=t;s->regulator_mode=~0u;
    if(!t.open || !t.escape || !t.close || !t.device_escape || !t.event_fd || !t.event_generation)
        return fail(s,"missing transport/event callbacks");
    if(!version || !*version || strlen(version)>=64)return fail(s,"invalid expected RM version");
    if(!strcmp(version,"595.99.02"))s->nvpcf_abi=595;
    else if(!strcmp(version,"620.70.06"))s->nvpcf_abi=620;
    else return fail(s,"no verified NVPCF layout for RM %s (known: 595.99.02, 620.70.06)",version);
    s->opened=true; /* close partial connection on open failure too */
    int r=t.open(t.opaque,true);if(r)return fail(s,"open ctl error %d",r);
    NvVersion v={0};snprintf(v.version,sizeof(v.version),"%s",version);
    if(esc(s,0xd2,&v,sizeof(v)))return -1;
    if(v.reply!=1)return fail(s,"strict RM version check failed (expected %s)",version);
    if(alloc(s,0,&s->root,0,NULL))return -1;
    uint8_t attach[132]={0};nv_put32(attach,0,0xffff);nv_put32(attach,4,NV_INVALID);
    if(nv_control(s,s->root,C_ATTACH,attach,sizeof(attach)))return -1;
    s->attached=true;
    uint8_t ids[128]={0};if(nv_control(s,s->root,C_IDS,ids,sizeof(ids)))return -1;
    s->gpu=nv_get32(ids,0);
    if(s->gpu==NV_INVALID || nv_get32(ids,4)!=NV_INVALID)return fail(s,"exactly one attached GPU is required");
    uint8_t id[40]={0};nv_put32(id,0,s->gpu);
    if(nv_control(s,s->root,C_ID_INFO,id,sizeof(id)))return -1;
    /* deviceInstance +8, subDeviceInstance +12, not a replayed GPU ID. */
    r=t.open(t.opaque,false);if(r)return fail(s,"open device error %d",r);
    uint8_t dev[56]={0};nv_put32(dev,0,nv_get32(id,8));s->device=0x20000000;
    if(alloc(s,s->root,&s->device,0x80,dev)) {s->device=0;return -1;}
    uint32_t sub=nv_get32(id,12);s->subdevice=0xceef0000;
    if(alloc(s,s->device,&s->subdevice,0x2080,&sub)){s->subdevice=0;return -1;}
    uint8_t pci[16]={0};if(nv_control(s,s->subdevice,C_PCIE,pci,sizeof(pci)))return -1;
    if(nv_get32(pci,0)!=0x2c5810de || nv_get32(pci,4)!=0x3b291043)
        return fail(s,"GPU PCI/subsystem ID differs from measured SCAR18 (2c58:10de/3b29:1043)");
    if(open_only)return 0; /* diagnostic dump: handles only, no policy/NVPCF/event setup */
    uint8_t conf[NVPCF_SIZE];
    if(nvpcf(s,2,conf))return -1;
    if(!(nv_get32(conf,0)&1))return fail(s,"NVPCF GET_SUPPORTED missing");
    uint8_t policy[POLICY_SIZE],dynamic[NVPCF_SIZE];
    if(nv_snapshot(s,policy,dynamic))return -1;
    uint8_t bios[48]={0};
    if(nv_control(s,s->subdevice,C_BIOS,bios,sizeof(bios)))return -1;
    /* 0x4dba40 tests projectSKUMod[0], byte +40, for the separate Q branch. */
    if(bios[40]=='Q')return fail(s,"Q BIOS branch needs a separate profile adapter");
    int rfd=t.event_fd(t.opaque,&s->event_fd);
    if(rfd)return fail(s,"event FD unavailable: %d",rfd);
    int rg=t.event_generation(t.opaque,&s->event_seq);
    if(rg)return fail(s,"event generation selector unavailable: %d",rg);
    return 0;
}
int nv_snapshot(NvSession *s,uint8_t policy[POLICY_SIZE],uint8_t dynamic[NVPCF_SIZE]){
    if(refresh_static(s))return -1;
    memset(policy,0,POLICY_SIZE);
    if(s->nvpcf_abi==620){
        uint8_t w[POLICY_SIZE_620]={0};
        if(nv_control(s,s->subdevice,C_POLICY,w,sizeof(w)))return -1;
        if(nv_policy_from620(w,policy,s->error,sizeof(s->error)))return -1;
        /* 595 slot-0 descriptor word 0x6060 reads 0 on GSP 620. */
        if(nv_policy_check_tag(policy,POLICY_SIZE,0,s->error,sizeof(s->error)))return -1;
    } else {
        if(nv_control(s,s->subdevice,C_POLICY,policy,POLICY_SIZE))return -1;
        if(nv_policy_check(policy,POLICY_SIZE,s->error,sizeof(s->error)))return -1;
    }
    if(query_info(s))return -1;
    return nvpcf(s,3,dynamic);
}
static int set_mw(NvSession *s,uint32_t mw){
    uint8_t b[SET_SIZE_620];size_t n=SET_SIZE;
    if(s->nvpcf_abi==620){
        /* Verified 2026-11-08 on SCAR18/NullMoth 2.8: GSP accepts the layout,
           sustained GPU clock 1115 -> 1450 MHz (150 W) and 1560 MHz (175 W). */
        nv_build_set_620(b,mw);n=SET_SIZE_620;
    } else nv_build_set(b,mw);
    /* Mark dirty before crossing transport: a failed reply can follow a successful write. */
    s->changed=true;
    if(nv_control(s,s->subdevice,C_SET,b,n))return -1;
    if(s->nvpcf_abi==620){ /* read-only evidence for the derived 620 setter layout */
        uint8_t w[POLICY_SIZE_620]={0};
        if(!nv_control(s,s->subdevice,C_POLICY,w,sizeof(w))){
            fprintf(stderr,"set %u mW; policy slot0 after:",mw);
            for(unsigned k=36;k<72;k+=4)fprintf(stderr," %u",nv_get32(w,k));
            fputc('\n',stderr);
        }
    }
    s->current_mw=mw;return 0;
}
int nv_tick(NvSession *s,uint32_t requested,bool mains){
    if(requested!=150000 && requested!=175000)return fail(s,"invalid requested limit");
    uint32_t generation=0;
    int rg=s->io.event_generation(s->io.opaque,&generation);
    if(rg)return fail(s,"event generation failed: %d",rg);
    if(generation!=s->event_seq){fprintf(stderr,"RM event generation %u -> %u; refreshing firmware/policy\n",s->event_seq,generation);s->event_seq=generation;}
    uint8_t policy[POLICY_SIZE],dyn[NVPCF_SIZE];uint32_t target;
    if(nv_snapshot(s,policy,dyn))return -1;
    if(nv_budget(dyn,sizeof(dyn),requested,&target,s->error,sizeof(s->error)))return -1;
    if(!mains)target=BASE_MW; /* never intentionally apply high AC budget on battery/unknown */
    if(!s->initialized){
        uint8_t enable=1;
        if(nv_control(s,s->subdevice,C_UPDATE,&enable,1))return -1;
        if(query_info(s))return -1;
        if(start_events(s))return -1;
        /* Register before high-power writes, then re-read to close the init race. */
        if(nv_snapshot(s,policy,dyn))return -1;
        if(nv_budget(dyn,sizeof(dyn),requested,&target,s->error,sizeof(s->error)))return -1;
        if(!mains)target=BASE_MW;
        s->initialized=true;
    }
    NvPowerProfile profile;NvDecision decision;NvCpuSample cpu={0};
    if(nv_profile(dyn,sizeof(dyn),&profile,s->error,sizeof(s->error)))return -1;
    if(profile.boost && mains && s->io.cpu_power){
        if(s->io.cpu_power(s->io.opaque,&cpu))cpu.valid=false;
    }
    if(nv_regulate(&s->regulator,&profile,&cpu,requested,s->current_mw,mains,&decision))
        return fail(s,"invalid feedback controller request");
    target=decision.target_mw;
    if(s->regulator_mode!=decision.mode || target!=s->current_mw){
        fprintf(stderr,"regulator mode=%u cpu=%u filtered=%u age_ms=%u target=%u total=%u\n",
                decision.mode,cpu.mw,decision.filtered_mw,cpu.age_ms,target,profile.total_mw);
        s->regulator_mode=decision.mode;
    }
    if(target<BASE_MW || target<s->policy.hardware_min_mw || target>s->policy.max_mw)
        return fail(s,"target outside allowed 95-W floor / freshly decoded PMGR bounds");
    if(s->current_mw!=target){
        if(set_mw(s,target))return -1;
        fprintf(stderr,"PMGR accepted %u mW (request %u, mains %d); effective limit is not measured here\n",target,requested,mains);
    }
    return 0;
}
int nv_end(NvSession *s){
    int bad=0;char first[256]={0};
    if(s->changed && s->root && s->subdevice && set_mw(s,BASE_MW)) {bad=-1;snprintf(first,sizeof(first),"reset: %.240s",s->error);}
    for(unsigned i=0;i<EVENT_COUNT;i++){
        if(s->notification_attempted[i] && notify(s,i,0)){
            if(!bad)snprintf(first,sizeof(first),"disable notifier %u: %.190s",event_ids[i],s->error);
            bad=-1;
        }
        if(s->events[i]){
            NvFree f={s->root,s->subdevice,s->events[i],NV_INVALID};
            if(esc(s,0x29,&f,sizeof(f)) || f.status){if(!bad)snprintf(first,sizeof(first),"free notifier %u failed",event_ids[i]);bad=-1;}
        }
        s->notification_attempted[i]=false;s->events[i]=0;
    }
    if(s->event_os){
        NvOsEvent p={s->root,s->device,s->event_fd,NV_INVALID};
        if(device_esc(s,0xcf,&p,sizeof(p)) || p.status){if(!bad)snprintf(first,sizeof(first),"FREE_OS_EVENT failed, status 0x%x",p.status);bad=-1;}
        s->event_os=false;
    }
    uint32_t objects[]={s->subdevice,s->device};
    uint32_t parents[]={s->device,s->root};
    for(unsigned i=0;i<2;i++)if(objects[i]){
        NvFree f={s->root,parents[i],objects[i],NV_INVALID};
        if(esc(s,0x29,&f,sizeof(f)) || f.status){if(!bad)snprintf(first,sizeof(first),"free object 0x%x status 0x%x",objects[i],f.status);bad=-1;}
    }
    /* Detach this client's refs, then free the root (recursively on partial errors). */
    if(s->attached && s->root){
        uint8_t b[128]={0};nv_put32(b,0,0xffff);nv_put32(b,4,NV_INVALID);
        if(nv_control(s,s->root,C_DETACH,b,sizeof(b))){if(!bad)snprintf(first,sizeof(first),"%s",s->error);bad=-1;}
    }
    if(s->root){NvFree f={s->root,0,s->root,NV_INVALID};
        if(esc(s,0x29,&f,sizeof(f)) || f.status){if(!bad)snprintf(first,sizeof(first),"free root failed (status 0x%x): %.170s",f.status,s->error);bad=-1;}}
    if(s->opened){int r=s->io.close(s->io.opaque);
        if(r){if(!bad)snprintf(first,sizeof(first),"transport close failed: %d",r);bad=-1;}
    }
    s->root=s->device=s->subdevice=0;s->opened=s->attached=s->changed=false;
    if(bad)snprintf(s->error,sizeof(s->error),"%s",first);
    return bad;
}
int nv_begin(NvSession *s,NvTransport t,const char *version){return begin(s,t,version,false);}
int nv_begin_open_only(NvSession *s,NvTransport t,const char *version){return begin(s,t,version,true);}
/* Read-only diagnostic: NVPCF dynamic reply in canonical layout. */
int nv_read_nvpcf(NvSession *s,unsigned sub,uint8_t out[NVPCF_SIZE]){
    if(sub!=2 && sub!=3 && sub!=4)return fail(s,"only NVPCF read sub-functions 2/3/4");
    if(sub==4)return refresh_static(s)?-1:(memcpy(out,s->static_conf,NVPCF_SIZE),0);
    return nvpcf(s,sub,out);
}

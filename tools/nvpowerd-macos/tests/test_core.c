/* SPDX-License-Identifier: MIT */
#include "nvpowerd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint8_t pol[POLICY_SIZE],supported[NVPCF_SIZE],conf[NVPCF_SIZE],dyn[3][NVPCF_SIZE],pci[16];
static uint8_t info[POLICY_INFO_SIZE],bios[48],notify_bytes[4][20];
static uint8_t expected_set[3][SET_SIZE];
static uint8_t pol620[POLICY_SIZE_620],info620[POLICY_INFO_SIZE_620];
static unsigned assertions;
#define CHECK(c) do{assertions++;if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void fixture(const char *dir,const char *name,void *b,size_t n){char p[1024];snprintf(p,sizeof(p),"%s/%s",dir,name);FILE*f=fopen(p,"rb");CHECK(f);CHECK(fread(b,1,n,f)==n);CHECK(fgetc(f)==EOF);CHECK(fclose(f)==0);}
typedef struct {int device_calls,os_allocs,os_frees,event_allocs,notifier_enables,notifier_disables,fd_error,gen_error,nth,fail_nth,mode,opens,closes,allocs,frees,sets,updates,attaches,detaches;uint32_t last,generation,cpu_mw;uint64_t cpu_seq;bool abi620,fail_close,fail_set,fail_reset,wrong_pci,wrong_version,fail_info,fail_cpu;uint32_t rm_fail;uint32_t commands[512];size_t count;} Mock;
/* 595 canonical -> 620 wire layout (inverse of nv_nvpcf_from620; TSP limit words dropped). */
static void to620(const uint8_t *in,uint8_t *out){memset(out,0,NVPCF_SIZE_620);memcpy(out,in,16);memcpy(out+20,in+16,4);
 memcpy(out+24,in+20,36);memcpy(out+60,in+60,4);memcpy(out+64,in+68,4);memcpy(out+68,in+72,28);memcpy(out+96,in+100,784);}
static int mo(void *p,bool ctl){Mock *m=p;m->opens++;CHECK(ctl==(m->opens==1));if(++m->nth==m->fail_nth)return 99;return 0;}
static int me(void *p,uint32_t nr,void *b,size_t n){
 Mock *m=p;
 if(++m->nth==m->fail_nth)return 99;
 if(nr==0xd2){CHECK(n==sizeof(NvVersion));NvVersion *v=b;v->reply=m->wrong_version?0:1;return 0;}
 if(nr==0x2b){CHECK(n==48);NvAlloc *a=b;CHECK(a->size==0);m->allocs++;a->status=0;
  if(a->cls==0){CHECK(!a->root && !a->parent);a->object=0x12340001;}
  else{CHECK(a->root==0x12340001);CHECK(a->cls==0x80 || a->cls==0x2080 || a->cls==0x79);
   if(a->cls==0x79){NvEventParams*e=(void*)(uintptr_t)a->params;
    CHECK(e->root==a->root && e->source==0 && e->cls==0x79 && e->data==20);
    CHECK(e->index==nv_get32(notify_bytes[m->event_allocs],0));m->event_allocs++;
   }CHECK(a->params!=0);}return 0;}
 if(nr==0x29){NvFree *f=b;CHECK(n==16);m->frees++;f->status=0;return 0;}
 CHECK(nr==0x2a && n==32);NvControl *c=b;CHECK(c->root==0x12340001);CHECK(m->count<512);m->commands[m->count++]=c->cmd;
 uint8_t *params=(void*)(uintptr_t)c->params;c->status=0;
 if(c->cmd==m->rm_fail){c->status=0x1f;return 0;}
 switch(c->cmd){
 case C_ATTACH:CHECK(c->size==132);CHECK(nv_get32(params,0)==0xffff);m->attaches++;break;
 case C_DETACH:CHECK(nv_get32(params,0)==0xffff);m->detaches++;break;
 case C_IDS:memset(params,0xff,128);nv_put32(params,0,0x200);break;
 case C_ID_INFO:nv_put32(params,8,0);nv_put32(params,12,0);break;
 case C_PCIE:memcpy(params,pci,16);if(m->wrong_pci)params[0]^=1;break;
 case C_NVPCF:{if(m->abi620){CHECK(c->size==NVPCF_SIZE_620);unsigned sub=nv_get32(params,20);CHECK(nv_get32(params,4)==0x200 && nv_get32(params,16)==0);
  if(sub==3)CHECK(params[70]==37 && nv_get32(params,880)==0); /* seeded from static config like nvidia-powerd */
  const uint8_t *src=sub==2?supported:sub==4?conf:dyn[m->mode];if(sub!=2&&sub!=4)CHECK(sub==3);to620(src,params);break;}
  CHECK(c->size==NVPCF_SIZE);unsigned sub=nv_get32(params,16);CHECK(nv_get32(params,4)==0x200);
  if(sub==2)memcpy(params,supported,NVPCF_SIZE);else if(sub==4)memcpy(params,conf,NVPCF_SIZE);else{CHECK(sub==3);CHECK(params[74]==37 && (params[76]|params[77]<<8)==100);memcpy(params,dyn[m->mode],NVPCF_SIZE);}break;}
 case C_POLICY:if(m->abi620){CHECK(c->size==POLICY_SIZE_620);memcpy(params,pol620,POLICY_SIZE_620);break;}
  CHECK(c->size==POLICY_SIZE);memcpy(params,pol,POLICY_SIZE);break;
 case C_POLICY_INFO:if(m->fail_info)return 99;if(m->abi620){CHECK(c->size==POLICY_INFO_SIZE_620);memcpy(params,info620,POLICY_INFO_SIZE_620);break;}
  CHECK(c->size==POLICY_INFO_SIZE);memcpy(params,info,POLICY_INFO_SIZE);break;
 case C_BIOS:CHECK(c->size==48);memcpy(params,bios,48);break;
 case C_NOTIFY:{CHECK(c->size==20);uint32_t action=nv_get32(params,4);
  if(action==2){CHECK(m->notifier_enables<4);CHECK(!memcmp(params,notify_bytes[m->notifier_enables],20));m->notifier_enables++;}
  else{CHECK(action==0);CHECK(m->notifier_disables<4);CHECK(nv_get32(params,0)==nv_get32(notify_bytes[m->notifier_disables],0));m->notifier_disables++;}break;}
 case C_UPDATE:CHECK(c->size==1 && params[0]==1);m->updates++;break;
 case C_SET:if(m->abi620){CHECK(c->size==SET_SIZE_620);uint32_t mw=nv_get32(params,44);
  CHECK(mw==95000 || (mw>=140000 && mw<=175000 && mw%500==0));
  for(unsigned k=0;k<SET_SIZE_620;k++){unsigned want=k==4?1:k==48?0xf7:(k>=44&&k<48)?params[k]:0;CHECK(params[k]==want);}
  CHECK(m->os_allocs==1 && m->notifier_enables==4 && m->updates==1);m->sets++;m->last=mw;break;}
  {CHECK(c->size==SET_SIZE);uint32_t mw=nv_get32(params,12);CHECK(mw==95000 || (mw>=140000 && mw<=175000 && mw%500==0));
  uint8_t expected[SET_SIZE];memcpy(expected,expected_set[1],SET_SIZE);nv_put32(expected,12,mw);
  CHECK(!memcmp(params,expected,SET_SIZE));
  CHECK(m->os_allocs==1 && m->notifier_enables==4 && m->updates==1);
  m->sets++;m->last=mw;
  if(m->fail_set || (m->fail_reset && mw==95000))return 5;
  break;}
 default:CHECK(0);
 }return 0;
}
static int mc(void *p){Mock*m=p;m->closes++;return m->fail_close?99:0;}
static int md(void*p,uint32_t nr,void*b,size_t n){Mock*m=p;CHECK(n==16 && (nr==0xce || nr==0xcf));NvOsEvent*e=b;
 CHECK(e->root==0x12340001 && e->device==0x20000000 && e->fd==20);m->device_calls++;
 if(++m->nth==m->fail_nth)return 99;
 if(nr==0xce)m->os_allocs++;else m->os_frees++;e->status=0;return 0;}
static int mf(void*p,uint32_t*fd){Mock*m=p;*fd=20;if(++m->nth==m->fail_nth)return 99;return m->fd_error;}
static int mg(void*p,uint32_t*seq){Mock*m=p;*seq=m->generation;if(++m->nth==m->fail_nth)return 99;return m->gen_error;}
static int mp(void*p,NvCpuSample*out){Mock*m=p;if(m->fail_cpu)return 99;*out=(NvCpuSample){true,m->cpu_mw?m->cpu_mw:10000,0,100,++m->cpu_seq};return 0;}
static NvTransport transport(Mock*m){return (NvTransport){m,mo,me,mc,md,mf,mg,mp};}
static void check_lifecycle(void){
 Mock m={0};NvSession s;CHECK(nv_begin(&s,transport(&m),"595.99.02")==0);
 CHECK(m.allocs==3 && m.sets==0 && m.updates==0 && m.event_allocs==0 && m.os_allocs==0); /* probe cannot perform a power write */
 m.mode=1;CHECK(nv_tick(&s,150000,true)==0);CHECK(m.last==150000 && m.updates==1);
 CHECK(m.os_allocs==1 && m.event_allocs==4 && m.notifier_enables==4);
 CHECK(nv_tick(&s,150000,true)==0);CHECK(m.sets==1); /* no constant setter traffic */
 m.generation=UINT32_MAX;m.mode=2;CHECK(nv_tick(&s,175000,true)==0);CHECK(m.last==150000);CHECK(nv_tick(&s,175000,true)==0);CHECK(m.last==175000 && s.event_seq==UINT32_MAX);
 m.generation=0;CHECK(nv_tick(&s,175000,true)==0 && s.event_seq==0);
 m.mode=1;CHECK(nv_tick(&s,175000,true)==0);CHECK(m.last==150000); /* honor firmware */
 m.mode=0;CHECK(nv_tick(&s,175000,true)==0);CHECK(m.last==95000);
 m.mode=2;CHECK(nv_tick(&s,175000,false)==0);CHECK(m.last==95000); /* battery */
 CHECK(nv_tick(&s,175000,true)==0);CHECK(m.last==150000);
 CHECK(nv_tick(&s,175000,true)==0);CHECK(m.last==175000);
 CHECK(nv_end(&s)==0);CHECK(m.last==95000 && m.frees==7 && m.detaches==1 && m.closes==1);
 CHECK(nv_end(&s)==0);CHECK(m.closes==1 && m.os_frees==1 && m.notifier_disables==4); /* idempotent cleanup */
}
static void check_failures(void){
 Mock feedback={.mode=2};NvSession fs;
 CHECK(nv_begin(&fs,transport(&feedback),"595.99.02")==0);
 CHECK(nv_tick(&fs,175000,true)==0 && feedback.last==150000);
 CHECK(nv_tick(&fs,175000,true)==0 && feedback.last==175000);
 feedback.cpu_mw=100000;CHECK(nv_tick(&fs,175000,true)==0 && feedback.last==170000);
 feedback.fail_cpu=true;
 for(unsigned k=0;k<CPU_GRACE_TICKS;k++)CHECK(nv_tick(&fs,175000,true)==0 && feedback.last==170000); /* grace hold */
 CHECK(nv_tick(&fs,175000,true)==0 && feedback.last==95000);
 feedback.fail_cpu=false;feedback.cpu_mw=10000;
 CHECK(nv_tick(&fs,175000,true)==0 && feedback.last==150000);
 CHECK(nv_tick(&fs,175000,true)==0 && feedback.last==175000);
 CHECK(nv_end(&fs)==0 && feedback.last==95000);
 Mock closing={.mode=1,.fail_close=true};NvSession cs;
 CHECK(nv_begin(&cs,transport(&closing),"595.99.02")==0);
 CHECK(nv_end(&cs)!=0 && strstr(cs.error,"transport close failed")!=NULL);
 CHECK(closing.closes==1);
 Mock m={.wrong_pci=true};NvSession s;CHECK(nv_begin(&s,transport(&m),"595.99.02")!=0);CHECK(m.sets==0);CHECK(nv_end(&s)==0);
 memset(&m,0,sizeof(m));m.wrong_version=true;CHECK(nv_begin(&s,transport(&m),"other")!=0);CHECK(m.allocs==0);CHECK(nv_end(&s)==0);
 memset(&m,0,sizeof(m));m.mode=1;CHECK(nv_begin(&s,transport(&m),"595.99.02")==0);m.rm_fail=C_POLICY;
 CHECK(nv_tick(&s,150000,true)!=0 && m.sets==0);CHECK(nv_end(&s)==0);
 memset(&m,0,sizeof(m));m.mode=1;CHECK(nv_begin(&s,transport(&m),"595.99.02")==0);m.fail_set=true;
 CHECK(nv_tick(&s,150000,true)!=0);m.fail_set=false;CHECK(nv_end(&s)==0);CHECK(m.last==95000); /* uncertain transport write cleaned */
 memset(&m,0,sizeof(m));m.mode=2;CHECK(nv_begin(&s,transport(&m),"595.99.02")==0);
 CHECK(nv_tick(&s,175000,true)==0);m.fail_reset=true;CHECK(nv_end(&s)!=0);CHECK(strstr(s.error,"reset") && m.closes==1 && m.frees==7);
 memset(&m,0,sizeof(m));m.mode=1;CHECK(nv_begin(&s,transport(&m),"595.99.02")==0);m.fail_info=true;
 CHECK(nv_tick(&s,150000,true)!=0 && m.sets==0);CHECK(nv_end(&s)==0);
 /* 620.70.06: same decisions through the 896-byte NVPCF wire layout. */
 memset(&m,0,sizeof(m));m.abi620=true;CHECK(nv_begin(&s,transport(&m),"620.70.06")==0);CHECK(s.nvpcf_abi==620 && m.sets==0);
 CHECK(s.policy.index==2 && s.policy.base_mw==95000 && s.policy.max_mw==175000 && s.policy.reference_mw==150000);
 m.mode=1;CHECK(nv_tick(&s,150000,true)==0);CHECK(m.last==150000);
 m.mode=2;CHECK(nv_tick(&s,175000,true)==0);CHECK(nv_tick(&s,175000,true)==0);CHECK(m.last==175000);
 m.mode=0;CHECK(nv_tick(&s,175000,true)==0);CHECK(m.last==95000);CHECK(nv_end(&s)==0);
 /* 620 policy refuses data beyond the 32-entry canonical layout. */
 {uint8_t w[POLICY_SIZE_620],o[POLICY_SIZE];char er[128];memcpy(w,pol620,sizeof(w));CHECK(nv_policy_from620(w,o,er,sizeof(er))==0);
  CHECK(nv_policy_check_tag(o,sizeof(o),0,er,sizeof(er))==0 && nv_get32(o,16)==95000);
  w[9]=1;CHECK(nv_policy_from620(w,o,er,sizeof(er))!=0);memcpy(w,pol620,sizeof(w));w[POLICY_SIZE_620-1]=1;CHECK(nv_policy_from620(w,o,er,sizeof(er))!=0);
  NvPolicyInfo pi;uint8_t *bi=malloc(POLICY_INFO_SIZE_620);CHECK(bi);memcpy(bi,info620,POLICY_INFO_SIZE_620);CHECK(nv_policy_info_620(bi,POLICY_INFO_SIZE_620,&pi,er,sizeof(er))==0 && pi.index==2);
  nv_put32(bi,116+2*260+0x10c,160000);CHECK(nv_policy_info_620(bi,POLICY_INFO_SIZE_620,&pi,er,sizeof(er))!=0);free(bi);}
 /* Unknown RM versions are refused before any allocation. */
 memset(&m,0,sizeof(m));CHECK(nv_begin(&s,transport(&m),"621.14")!=0);CHECK(m.allocs==0 && strstr(s.error,"NVPCF layout"));CHECK(nv_end(&s)==0);
 /* Converter round trip on every fixture. */
 for(unsigned k=0;k<3;k++){uint8_t w[NVPCF_SIZE_620],back[NVPCF_SIZE],ref[NVPCF_SIZE];memcpy(ref,dyn[k],NVPCF_SIZE);
  memset(ref+56,0,4);memset(ref+64,0,4);to620(dyn[k],w);nv_nvpcf_from620(w,back);CHECK(!memcmp(back,ref,NVPCF_SIZE));}
}
int main(int argc,char **argv){
 CHECK(argc==2);fixture(argv[1],"policy.bin",pol,sizeof(pol));fixture(argv[1],"pci.bin",pci,sizeof(pci));
 fixture(argv[1],"supported.bin",supported,sizeof(supported));fixture(argv[1],"static.bin",conf,sizeof(conf));
 const char *names[]={"quiet.bin","balanced.bin","performance.bin"};
 const char *sets[]={"set-95000.bin","set-150000.bin","set-175000.bin"};
 for(unsigned i=0;i<3;i++){fixture(argv[1],names[i],dyn[i],NVPCF_SIZE);fixture(argv[1],sets[i],expected_set[i],SET_SIZE);}
 fixture(argv[1],"policy_info.bin",info,sizeof(info));
 fixture(argv[1],"policy620.bin",pol620,sizeof(pol620));fixture(argv[1],"policy_info620.bin",info620,sizeof(info620));fixture(argv[1],"bios.bin",bios,sizeof(bios));
 const char *nf[]={"notify-118.bin","notify-181.bin","notify-177.bin","notify-33.bin"};
 for(unsigned i=0;i<4;i++)fixture(argv[1],nf[i],notify_bytes[i],20);
 char error[256];uint32_t target;CHECK(nv_policy_check(pol,sizeof(pol),error,sizeof(error))==0);
 CHECK(nv_policy_check(pol,12,error,sizeof(error))!=0);uint8_t corrupt[POLICY_SIZE];memcpy(corrupt,pol,sizeof(pol));corrupt[4]=2;CHECK(nv_policy_check(corrupt,sizeof(corrupt),error,sizeof(error))!=0);
 memcpy(corrupt,pol,sizeof(pol));nv_put32(corrupt,40,175000);
 CHECK(nv_policy_check(corrupt,sizeof(corrupt),error,sizeof(error))==0); /* not assumed immutable */
 corrupt[10]=3;CHECK(nv_policy_check(corrupt,sizeof(corrupt),error,sizeof(error))!=0);
 CHECK(nv_budget(dyn[1],NVPCF_SIZE,150000,&target,error,sizeof(error))==0 && target==150000);
 CHECK(nv_budget(dyn[2],NVPCF_SIZE,175000,&target,error,sizeof(error))==0 && target==175000);
 uint8_t b[NVPCF_SIZE];memcpy(b,dyn[2],sizeof(b));nv_put32(b,20,0xffffffffu);CHECK(nv_budget(b,sizeof(b),175000,&target,error,sizeof(error))!=0);
 CHECK(nv_budget(dyn[2],10,175000,&target,error,sizeof(error))!=0);
 CHECK(nv_budget(dyn[2],NVPCF_SIZE,250000,&target,error,sizeof(error))!=0);
 NvPolicyInfo pi;CHECK(nv_policy_info(info,sizeof(info),&pi,error,sizeof(error))==0);
 CHECK(pi.index==2 && pi.hardware_min_mw==5000 && pi.base_mw==95000 && pi.max_mw==175000 && pi.reference_mw==150000);
 uint8_t bad_info[POLICY_INFO_SIZE];memcpy(bad_info,info,sizeof(info));bad_info[40]=255;bad_info[42]=255;
 CHECK(nv_policy_info(bad_info,sizeof(bad_info),&pi,error,sizeof(error))!=0);
 memcpy(bad_info,info,sizeof(info));bad_info[40]=32;
 CHECK(nv_policy_info(bad_info,sizeof(bad_info),&pi,error,sizeof(error))!=0);
 memcpy(bad_info,info,sizeof(info));nv_put32(bad_info,4,0);
 CHECK(nv_policy_info(bad_info,sizeof(bad_info),&pi,error,sizeof(error))!=0);
 memcpy(bad_info,info,sizeof(info));bad_info[2*228+0x5e]=1;
 CHECK(nv_policy_info(bad_info,sizeof(bad_info),&pi,error,sizeof(error))!=0);
 memcpy(bad_info,info,sizeof(info));nv_put32(bad_info,2*228+0x68,250000);
 CHECK(nv_policy_info(bad_info,sizeof(bad_info),&pi,error,sizeof(error))!=0);
 CHECK(nv_policy_info(info,12,&pi,error,sizeof(error))!=0);
 check_lifecycle();check_failures();
 Mock baseline={0};NvSession bs;CHECK(nv_begin(&bs,transport(&baseline),"595.99.02")==0);
 int init_count=baseline.nth;CHECK(nv_end(&bs)==0);
 for(int i=1;i<=init_count;i++){
  Mock m={.fail_nth=i};NvSession s;
  int result=nv_begin(&s,transport(&m),"595.99.02");
  CHECK(result!=0);CHECK(m.sets==0);m.fail_nth=0;
  CHECK(nv_end(&s)==0);CHECK(m.closes==1);
 }
 Mock prepared={.mode=1};NvSession ps;CHECK(nv_begin(&ps,transport(&prepared),"595.99.02")==0);
 int prepare_start=prepared.nth;CHECK(nv_tick(&ps,150000,true)==0);int prepare_end=prepared.nth;
 CHECK(nv_end(&ps)==0);
 for(int i=prepare_start+1;i<=prepare_end;i++){
  Mock m={.mode=1,.fail_nth=i};NvSession s;
  CHECK(nv_begin(&s,transport(&m),"595.99.02")==0);
  CHECK(nv_tick(&s,150000,true)!=0);m.fail_nth=0;
  CHECK(nv_end(&s)==0);CHECK(m.closes==1);
 }
 printf("PASS: %u assertions using captured bytes and mock RM lifecycle (no hardware writes)\n",assertions);return 0;
}

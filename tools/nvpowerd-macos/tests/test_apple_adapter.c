/* SPDX-License-Identifier: MIT */
#define NVPOWERD_TEST_APPLE_API 1
#define main nvpowerd_cli_main
#include "../src/macos.c"
#undef main
#include <assert.h>
static int generation_failure;
static int close_failure;
static int mode,kind=1,opens,closes,binds,registers,lookups;
static int checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"adapter FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
kern_return_t IOConnectCallMethod(io_connect_t c,uint32_t sel,const uint64_t *in,uint32_t ni,const void *p,size_t n,uint64_t *o,uint32_t *no,void *data,size_t *out){
 (void)p;(void)o;(void)no;CHECK(c!=0 && sel==1 && ni==1);
 if(*in==0xc9){CHECK(n==4);registers++;return 0;}
 CHECK(*in==0x2a && n==sizeof(NvControl));NvControl *a=data;
 a->status=0;
 if(mode){a->params=1;a->size=4096;} /* a malformed or partially copied outer reply */
 if(mode==1){*out=8;return KERN_FAILURE;}
 return 0;
}
kern_return_t IOConnectCallScalarMethod(io_connect_t c,uint32_t sel,const uint64_t *i,uint32_t n,uint64_t *o,uint32_t *no){
 CHECK(c);
 if(sel==6){CHECK(n==2 && i[1]==0 && o && no && *no==1);if(generation_failure)return KERN_FAILURE;*o=UINT32_MAX;return 0;}
 CHECK(sel==3 && n==1 && *i<1024);binds++;return 0;
}
void *IOServiceMatching(const char *s){return (void*)s;}
io_service_t IOServiceGetMatchingService(uint32_t p,void*d){(void)p;(void)d;lookups++;return 1;}
kern_return_t IOServiceOpen(io_service_t s,uint32_t t,uint32_t ty,io_connect_t*c){(void)t;CHECK(s && (ty==0x4e560000u || ty==0x4e560001u));*c=(io_connect_t)(++opens);return 0;}
int IOObjectRelease(io_object_t p){CHECK(p!=0);return 0;}
int IOServiceClose(io_connect_t c){CHECK(c!=0);closes++;return close_failure?KERN_FAILURE:0;}
uint32_t mach_task_self(void){return 1;}
CFTypeRef IOPSCopyPowerSourcesInfo(void){return kind<0?NULL:(void*)1;}
CFStringRef IOPSGetProvidingPowerSourceType(CFTypeRef p){CHECK(p);return kind==1?"AC Power":kind==0?"Battery Power":NULL;}
int CFEqual(const void*a,const void*b){return strcmp(a,b)==0;}
void CFRelease(const void*p){(void)p;}
void *CFRunLoopGetCurrent(void){return (void*)1;}
void CFRunLoopStop(void*p){(void)p;}
void CFRunLoopRun(void){}
io_connect_t IORegisterForSystemPower(void*p,IONotificationPortRef*q,void(*fn)(void*,io_service_t,natural_t,void*),io_object_t*n){(void)p;(void)fn;*q=(void*)1;*n=1;return 1;}
void *IONotificationPortGetRunLoopSource(IONotificationPortRef p){return p;}
void IONotificationPortDestroy(IONotificationPortRef p){(void)p;}
void CFRunLoopAddSource(void*a,void*b,void*c){(void)a;(void)b;(void)c;}
void CFRunLoopAddTimer(void*a,CFRunLoopTimerRef b,void*c){(void)a;(void)b;(void)c;}
double CFAbsoluteTimeGetCurrent(void){return 0;}
CFRunLoopTimerRef CFRunLoopTimerCreate(void*a,double b,double c,unsigned long d,long e,void(*f)(CFRunLoopTimerRef,void*),CFRunLoopTimerContext*g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;return (void*)1;}
void CFRunLoopTimerInvalidate(CFRunLoopTimerRef t){(void)t;}
int IOAllowPowerChange(io_connect_t c,intptr_t a){(void)c;(void)a;return 0;}
int IODeregisterForSystemPower(io_object_t*p){*p=0;return 0;}
int main(void){
 MacIO m={0};m.ctl_fd=m.dev_fd=-1;m.trace=tmpfile();CHECK(m.trace);
 CHECK(mac_open(&m,true)==0);CHECK(mac_open(&m,false)==0);
 CHECK(opens==2 && binds==2 && registers==1 && lookups==2);
 uint32_t fd,seq;CHECK(mac_event_fd(&m,&fd)==0 && fd==(uint32_t)m.dev_fd);
 CHECK(mac_event_generation(&m,&seq)==0 && seq==UINT32_MAX);
 generation_failure=1;CHECK(mac_event_generation(&m,&seq)!=0);generation_failure=0;
 uint8_t data[SET_SIZE];nv_build_set(data,150000);
 NvControl c={1,2,C_SET,0,(uint64_t)(uintptr_t)data,SET_SIZE,NV_INVALID};
 CHECK(call(&m,m.ctl,0x2a,&c,sizeof(c))==0);
 mode=1;c.params=(uint64_t)(uintptr_t)data;c.size=SET_SIZE;
 CHECK(call(&m,m.ctl,0x2a,&c,sizeof(c))!=0); /* must not dereference returned pointer 1 */
 mode=2;c.params=(uint64_t)(uintptr_t)data;c.size=SET_SIZE;
 CHECK(call(&m,m.ctl,0x2a,&c,sizeof(c))==0); /* logging still follows original trusted buffer */
 CHECK(mains());kind=0;CHECK(!mains());kind=-1;CHECK(!mains());kind=2;CHECK(!mains());
 CHECK(mac_close(&m)==0);CHECK(closes==2 && m.ctl_fd==-1 && m.dev_fd==-1);CHECK(mac_close(&m)==0);CHECK(closes==2);
 CHECK(mac_open(&m,true)==0);CHECK(mac_open(&m,false)==0);
 close_failure=1;CHECK(mac_close(&m)!=0);CHECK(m.ctl==0 && m.dev==0 && m.ctl_fd==-1 && m.dev_fd==-1);
 fclose(m.trace);printf("PASS: %d Apple-adapter checks with API doubles, NOT an SDK build\n",checks);return 0;
}

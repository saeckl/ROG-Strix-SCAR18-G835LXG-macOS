/* SPDX-License-Identifier: MIT – real framing/process cleanup, CF API doubles. */
#define NVPOWERD_TEST_CPU_API 1
#include "../src/cpu_source.c"
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"CPU-source FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {unsigned long type;double value;} Item;
static Item root={1,0},processor={1,0},delta={4,1},stamp={3,1000},energy={2,8},elapsed={2,1e8};
static bool omit_energy,malformed;
CFDataRef CFDataCreate(void*a,const unsigned char*b,CFIndex n){(void)a;(void)b;CHECK(n>0);return &root;}
CFPropertyListRef CFPropertyListCreateWithData(void*a,CFDataRef b,int c,void*d,void*e){(void)a;(void)b;(void)c;(void)d;(void)e;return malformed?NULL:&root;}
void CFRelease(CFTypeRef p){(void)p;}
unsigned long CFGetTypeID(CFTypeRef p){return ((const Item*)p)->type;}
unsigned long CFDictionaryGetTypeID(void){return 1;}unsigned long CFNumberGetTypeID(void){return 2;}
unsigned long CFDateGetTypeID(void){return 3;}unsigned long CFBooleanGetTypeID(void){return 4;}
CFTypeRef CFDictionaryGetValue(CFDictionaryRef d,CFStringRef k){
    if(d==&root){if(!strcmp(k,"processor"))return &processor;if(!strcmp(k,"is_delta"))return &delta;if(!strcmp(k,"timestamp"))return &stamp;if(!strcmp(k,"elapsed_ns"))return &elapsed;}
    if(d==&processor && !strcmp(k,"package_joules") && !omit_energy)return &energy;
    return NULL;
}
int CFNumberGetValue(CFNumberRef p,int t,void*out){(void)t;*(double*)out=((const Item*)p)->value;return 1;}
int CFBooleanGetValue(CFBooleanRef p){return ((const Item*)p)->value!=0;}
double CFAbsoluteTimeGetCurrent(void){return 1000.5;}double CFDateGetAbsoluteTime(CFDateRef p){return ((const Item*)p)->value;}
int main(void){
    int fds[2];CHECK(pipe(fds)==0);CHECK(fcntl(fds[0],F_SETFL,O_NONBLOCK)==0);
    NvCpuSource s={0};s.started=true;s.pid=-1;s.fd=fds[0];s.frame=malloc(FRAME_CAP);CHECK(s.frame);s.poll_ns=monotonic_ns();NvCpuSample out;
    CHECK(write(fds[1],"sample",6)==6);CHECK(nv_cpu_read(&s,&out)!=0 && s.used==6);
    CHECK(write(fds[1],"\0",1)==1);CHECK(nv_cpu_read(&s,&out)==0 && out.valid && out.mw==80000 && out.interval_ms==100 && out.seq==1);
    CHECK(nv_cpu_read(&s,&out)==0 && out.seq==1); /* cached sample age, not re-filtered */
    omit_energy=true;CHECK(write(fds[1],"x\0",2)==2);CHECK(nv_cpu_read(&s,&out)!=0 && !out.valid);omit_energy=false;
    malformed=true;CHECK(write(fds[1],"x\0",2)==2);CHECK(nv_cpu_read(&s,&out)!=0);malformed=false;
    delta.value=0;CHECK(write(fds[1],"x\0",2)==2);CHECK(nv_cpu_read(&s,&out)!=0);delta.value=1;
    stamp.value=990;CHECK(write(fds[1],"x\0",2)==2);CHECK(nv_cpu_read(&s,&out)!=0);stamp.value=1000;
    elapsed.value=1e9;CHECK(write(fds[1],"x\0",2)==2);CHECK(nv_cpu_read(&s,&out)!=0);elapsed.value=1e8;
    CHECK(write(fds[1],"x\0x\0x\0",6)==6);CHECK(nv_cpu_read(&s,&out)!=0 && !s.last_ns); /* backlog */
    s.poll_ns=monotonic_ns()-300000000u;CHECK(write(fds[1],"x\0",2)==2);CHECK(nv_cpu_read(&s,&out)!=0 && !s.last_ns);
    CHECK(write(fds[1],"x\0",2)==2);CHECK(nv_cpu_read(&s,&out)==0);
    s.last_ns=monotonic_ns()-400000000u;CHECK(nv_cpu_read(&s,&out)!=0);
    close(fds[1]);CHECK(nv_cpu_read(&s,&out)!=0 && s.failed);CHECK(nv_cpu_stop(&s)==0 && !s.started);CHECK(nv_cpu_stop(&s)==0);
    pid_t child=fork();CHECK(child>=0);
    if(child==0){for(;;)pause();}
    s.started=true;s.pid=(int)child;s.fd=-1;
    CHECK(nv_cpu_stop(&s)==0);int status=0;CHECK(waitpid(child,&status,WNOHANG)==-1 && errno==ECHILD);
    printf("PASS: %u CPU-source framing/schema checks with CF API doubles (NOT SDK)\n",checks);return 0;
}

/* SPDX-License-Identifier: MIT
 * Native IOKit client. Independently implemented from the published selector ABI.
 * No NVIDIA/NullMoth binary or source library is linked into this executable. */
#include "nvpowerd.h"
#include "cpu_source.h"
#if defined(__APPLE__) || defined(NVPOWERD_TEST_APPLE_API)
#ifdef NVPOWERD_TEST_APPLE_API
#include "apple_api_mock.h"
#else
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/IOMessage.h>
#include <mach/mach.h>
#endif
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {io_connect_t ctl,dev;int ctl_fd,dev_fd;FILE *trace;NvCpuSource cpu;bool cpu_attempted;} MacIO;
static int call(MacIO *m,io_connect_t conn,uint32_t nr,void *p,size_t size){
    if(!conn)return ENOTCONN;
    uint64_t arg=nr;size_t out=size;
    /* Retain caller-owned metadata: the returned outer structure may be partial
       or corrupt on a failed copyout. Never dereference its returned pointer. */
    NvControl input={0};
    bool control=(nr==0x2a && size==sizeof(NvControl));
    if(control)memcpy(&input,p,sizeof(input));
    kern_return_t kr=IOConnectCallMethod(conn,1,&arg,1,p,size,NULL,NULL,p,&out);
    if(m->trace){
        struct timespec now={0};(void)clock_gettime(CLOCK_MONOTONIC,&now);
        fprintf(m->trace,"%lld.%09ld escape=0x%x size=%zu IOKit=0x%x out=%zu",
                (long long)now.tv_sec,now.tv_nsec,nr,size,kr,out);
        if(control){
            uint32_t status=NV_INVALID;
            if(kr==KERN_SUCCESS && out==size)memcpy(&status,(uint8_t*)p+offsetof(NvControl,status),sizeof(status));
            fprintf(m->trace," cmd=0x%08x client=0x%x object=0x%x RM=0x%x",input.cmd,input.root,input.object,status);
            if(kr==KERN_SUCCESS && out==size && input.size<=POLICY_INFO_SIZE_620 &&
               (input.cmd==C_NVPCF || input.cmd==C_SET_GET || input.cmd==C_POLICY || input.cmd==C_POLICY_INFO || input.cmd==C_SET || input.cmd==C_NOTIFY)){
                const uint8_t *b=(const void*)(uintptr_t)input.params;
                fputs(" params_after=",m->trace);
                for(uint32_t i=0;i<input.size;i++)fprintf(m->trace,"%02x",b[i]);
            }
        }
        fputc('\n',m->trace);fflush(m->trace);
    }
    if(kr!=KERN_SUCCESS || out!=size){fprintf(stderr,"IOKit escape 0x%x error=0x%x size=%zu/%zu\n",nr,kr,out,size);return EIO;}
    return 0;
}
static int open_conn(io_connect_t *conn,int *fd,bool ctl){
    const char *classes[]={"NVAccel","NVRM"};uint32_t type=ctl?0:1;
    kern_return_t kr=KERN_FAILURE;
    for(unsigned i=0;i<2;i++){
        io_service_t svc=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching(classes[i]));
        if(!svc)continue;
        *conn=0;
        kr=IOServiceOpen(svc,mach_task_self(),i?type:(0x4e560000u|type),conn);
        IOObjectRelease(svc);if(kr==KERN_SUCCESS)break;
    }
    if(kr!=KERN_SUCCESS){fprintf(stderr,"open %s IOKit error=0x%x\n",ctl?"ctl":"device",kr);return ENODEV;}
    *fd=open("/dev/null",O_RDONLY|O_CLOEXEC);
    if(*fd<0)return errno;
    uint64_t bound=(uint64_t)*fd;
    kr=IOConnectCallScalarMethod(*conn,3,&bound,1,NULL,NULL);
    return kr==KERN_SUCCESS?0:EIO;
}
static int mac_open(void *v,bool ctl){
    MacIO *m=v;
    if(ctl)return open_conn(&m->ctl,&m->ctl_fd,true);
    int r=open_conn(&m->dev,&m->dev_fd,false);if(r)return r;
    /* Linux REGISTER_FD associates this device connection with the ctl client. */
    uint32_t fd=(uint32_t)m->ctl_fd;
    return call(m,m->dev,0xc9,&fd,sizeof(fd));
}
static int mac_esc(void *v,uint32_t nr,void *p,size_t n){MacIO *m=v;return call(m,m->ctl,nr,p,n);}
static int mac_device_esc(void *v,uint32_t nr,void *p,size_t n){MacIO *m=v;return call(m,m->dev,nr,p,n);}
static int mac_event_fd(void *v,uint32_t *fd){MacIO *m=v;if(m->dev_fd<0)return EBADF;*fd=(uint32_t)m->dev_fd;return 0;}
static int mac_event_generation(void *v,uint32_t *seq){
    MacIO *m=v;if(!m->dev)return ENOTCONN;
    uint64_t in[2]={0,0},out=UINT64_MAX;uint32_t n=1;
    kern_return_t kr=IOConnectCallScalarMethod(m->dev,6,in,2,&out,&n);
    if(kr!=KERN_SUCCESS || n!=1 || out>UINT32_MAX){
        fprintf(stderr,"event selector 6 failed: IOKit=0x%x outputs=%u\n",kr,n);return EIO;
    }
    *seq=(uint32_t)out;return 0;
}
static int mac_cpu_power(void *v,NvCpuSample *sample){
    MacIO *m=v;
    if(!m->cpu_attempted){
        m->cpu_attempted=true;int e=nv_cpu_start(&m->cpu,m->trace);
        if(e){fprintf(stderr,"CPU package source unavailable (%d); no Dynamic Boost\n",e);return e;}
    }
    return nv_cpu_read(&m->cpu,sample);
}
static int mac_close(void *v){MacIO *m=v;int bad=nv_cpu_stop(&m->cpu);m->cpu_attempted=false;
    if(m->dev){kern_return_t kr=IOServiceClose(m->dev);if(kr!=KERN_SUCCESS){fprintf(stderr,"device close failed: 0x%x\n",kr);bad=EIO;}m->dev=0;}
    if(m->ctl){kern_return_t kr=IOServiceClose(m->ctl);if(kr!=KERN_SUCCESS){fprintf(stderr,"ctl close failed: 0x%x\n",kr);bad=EIO;}m->ctl=0;}
    if(m->dev_fd>=0){if(close(m->dev_fd))bad=errno;m->dev_fd=-1;}
    if(m->ctl_fd>=0){if(close(m->ctl_fd))bad=errno;m->ctl_fd=-1;}
    return bad;
}
static bool mains(void){
    CFTypeRef info=IOPSCopyPowerSourcesInfo();if(!info)return false;
    CFStringRef kind=IOPSGetProvidingPowerSourceType(info);
    bool ac=kind && CFEqual(kind,CFSTR(kIOPSACPowerValue));CFRelease(info);return ac;
}
static volatile sig_atomic_t stop_requested;
static void on_signal(int n){(void)n;stop_requested=1;}
typedef struct {
    NvSession s;MacIO mac;NvTransport t;const char *version;uint32_t requested;
    bool live,suspended;int result;io_connect_t power;IONotificationPortRef notify;
    io_object_t notifier;CFRunLoopTimerRef timer;
} App;
static int cleanup(App *a){
    if(!a->s.opened){a->live=false;return 0;}
    int r=nv_end(&a->s);a->live=false;
    if(r)fprintf(stderr,"cleanup error: %s; the 95 W fallback is NOT confirmed\n",a->s.error);
    return r;
}
static int start(App *a){
    if(nv_begin(&a->s,a->t,a->version)){
        fprintf(stderr,"initialization failed: %s\n",a->s.error);cleanup(a);return -1;
    }
    a->live=true;return 0;
}
static void timer_fn(CFRunLoopTimerRef timer,void *ctx){
    (void)timer;App *a=ctx;
    if(stop_requested){CFRunLoopStop(CFRunLoopGetCurrent());return;}
    if(a->suspended)return;
    if(!a->live && start(a)){a->result=1;CFRunLoopStop(CFRunLoopGetCurrent());return;}
    if(nv_tick(&a->s,a->requested,mains())){
        fprintf(stderr,"control cycle failed: %s\n",a->s.error);a->result=1;CFRunLoopStop(CFRunLoopGetCurrent());
    }
}
static void power_fn(void *ctx,io_service_t service,natural_t type,void *arg){
    (void)service;App *a=ctx;
    if(type==kIOMessageCanSystemSleep){IOAllowPowerChange(a->power,(intptr_t)arg);}
    else if(type==kIOMessageSystemWillSleep){
        a->suspended=true;
        if(cleanup(a)){a->result=1;stop_requested=1;CFRunLoopStop(CFRunLoopGetCurrent());}
        IOAllowPowerChange(a->power,(intptr_t)arg);
    } else if(type==kIOMessageSystemHasPoweredOn){
        /* Fresh version/GPU/policy/NVPCF checks on the next timer; no stale handles. */
        a->suspended=false;
    }
}
static int usage(void){
    fprintf(stderr,"nvpowerd-macos 0.5 (SCAR18 profile controller, RM 595.99.02 and 620.70.06)\n"
      "Usage: nvpowerd-macos --probe --rm-version VERSION [--trace FILE]\n"
      "       nvpowerd-macos --serve --watts 150|175 --rm-version VERSION [--trace FILE]\n"
      "       nvpowerd-macos --dump-pmgr DIR --rm-version VERSION [--trace FILE]\n"
      "Dump reads PMGR GET controls with the GSP 620 sizes into DIR (read-only, no setters).\n"
      "Probe allocates an RM client and reads; it sends no PMGR setters/UPDATE_TGP.\n"
      "Serve stays in foreground; SIGTERM attempts 95 W fallback and closes owned handles.\n");return 2;
}
/* Read-only PMGR layout dump. Only controls whose GSP 620 table entry carries
   API_LOCK_READONLY (GET semantics) are issued; no setter, no UPDATE_TGP, no events. */
static int dump_pmgr(App *a,const char *dir){
    static const struct {uint32_t cmd,size;const char *name;} q[]={
        {0x2080a630u,2340u,"a630"},{0x2080a618u,20512u,"a618"},{0x2080a631u,804u,"a631"},{0x2080a632u,808u,"a632"}};
    if(nv_begin_open_only(&a->s,a->t,a->version)){fprintf(stderr,"open failed: %s\n",a->s.error);return 1;}
    a->live=true;int bad=0;char path[1024];
    static const unsigned subs[]={2,4,3};
    for(unsigned si=0;si<3;si++){unsigned sub=subs[si];
        uint8_t b[NVPCF_SIZE];snprintf(path,sizeof(path),"%s/nvpcf-sub%u.bin",dir,sub);
        if(nv_read_nvpcf(&a->s,sub,b)){fprintf(stderr,"nvpcf %u: %s\n",sub,a->s.error);bad++;continue;}
        FILE *f=fopen(path,"wb");if(!f||fwrite(b,1,sizeof(b),f)!=sizeof(b)){perror(path);bad++;}if(f)fclose(f);
    }
    for(size_t i=0;i<sizeof(q)/sizeof(q[0]);i++){
        uint8_t *b=calloc(1,q[i].size);if(!b)return 1;
        int r=nv_control(&a->s,a->s.subdevice,q[i].cmd,b,q[i].size);
        printf("0x%08x size %u: %s\n",q[i].cmd,q[i].size,r?a->s.error:"OK");
        if(!r){snprintf(path,sizeof(path),"%s/%s.bin",dir,q[i].name);
            FILE *f=fopen(path,"wb");if(!f||fwrite(b,1,q[i].size,f)!=q[i].size){perror(path);bad++;}if(f)fclose(f);}
        else bad++;
        free(b);
    }
    { /* read-only GET of the setter structure for slot 0 (mask bit 0) */
        uint8_t g[SET_SIZE_620]={0};nv_put32(g,4,1);
        int r=nv_control(&a->s,a->s.subdevice,C_SET_GET,g,sizeof(g));
        printf("0x%08x mask0 size %u: %s\n",C_SET_GET,SET_SIZE_620,r?a->s.error:"OK");
        if(!r){snprintf(path,sizeof(path),"%s/a632-mask0.bin",dir);FILE *f=fopen(path,"wb");if(!f||fwrite(g,1,sizeof(g),f)!=sizeof(g)){perror(path);bad++;}if(f)fclose(f);}
        else bad++;
    }
    printf("dump done, %d failures; NO power limit written\n",bad);
    return bad?1:0;
}
int main(int argc,char **argv){
    App a={0};a.mac.ctl_fd=a.mac.dev_fd=-1;bool probe=false,serve=false;const char *logpath=NULL,*dumpdir=NULL;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--probe"))probe=true;
        else if(!strcmp(argv[i],"--serve"))serve=true;
        else if(!strcmp(argv[i],"--dump-pmgr") && i+1<argc){dumpdir=argv[++i];probe=true;}
        else if(!strcmp(argv[i],"--watts") && i+1<argc){const char *w=argv[++i];
            if(!strcmp(w,"150"))a.requested=150000;else if(!strcmp(w,"175"))a.requested=175000;else return usage();}
        else if(!strcmp(argv[i],"--rm-version") && i+1<argc)a.version=argv[++i];
        else if(!strcmp(argv[i],"--trace") && i+1<argc)logpath=argv[++i];
        else return usage();
    }
    if(probe==serve || !a.version || !*a.version || strlen(a.version)>=64 ||
       (serve && !a.requested) || (probe && a.requested))return usage();
    if(geteuid()!=0){fprintf(stderr,"Root is required for this RM client.\n");return 1;}
    umask(077);
    int lockfd=open("/var/run/nvpowerd-macos.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lockfd<0 || flock(lockfd,LOCK_EX|LOCK_NB)) {fprintf(stderr,"another instance or lock error\n");if(lockfd>=0)close(lockfd);return 1;}
    if(logpath){
        int f=open(logpath,O_CREAT|O_WRONLY|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
        if(f<0 || !(a.mac.trace=fdopen(f,"a"))){perror("trace");if(f>=0)close(f);close(lockfd);return 1;}
    }
    a.t=(NvTransport){&a.mac,mac_open,mac_esc,mac_close,mac_device_esc,mac_event_fd,mac_event_generation,mac_cpu_power};
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);signal(SIGHUP,on_signal);
    /* Driver readiness: version/probe failures exit visibly. launchd retry is throttled. */
    if(dumpdir){a.result=dump_pmgr(&a,dumpdir);goto done;}
    if(start(&a)){a.result=1;goto done;}
    if(probe){
        uint8_t policy[POLICY_SIZE],dynamic[NVPCF_SIZE];uint32_t limit;
        if(nv_snapshot(&a.s,policy,dynamic)||nv_budget(dynamic,sizeof(dynamic),175000,&limit,a.s.error,sizeof(a.s.error))){
            fprintf(stderr,"probe failed: %s\n",a.s.error);a.result=1;
        } else fprintf(stdout,"SCAR18 layout matched; GPU=0x%x firmware cap=%u mW mains=%d; policy=%u hw_min=%u base=%u max=%u reference=%u; event selector verified (no subscriptions); cap is NOT a stable boost guarantee; CPU feedback required when AC boost enabled; NO power limit written\n",a.s.gpu,limit,mains(),a.s.policy.index,a.s.policy.hardware_min_mw,a.s.policy.base_mw,a.s.policy.max_mw,a.s.policy.reference_mw);
        goto done;
    }
    a.power=IORegisterForSystemPower(&a,&a.notify,power_fn,&a.notifier);
    if(!a.power || !a.notify){fprintf(stderr,"power notifications unavailable; not starting setters\n");a.result=1;goto done;}
    CFRunLoopAddSource(CFRunLoopGetCurrent(),IONotificationPortGetRunLoopSource(a.notify),kCFRunLoopCommonModes);
    CFRunLoopTimerContext tc={0,&a,NULL,NULL,NULL};
    a.timer=CFRunLoopTimerCreate(NULL,CFAbsoluteTimeGetCurrent(),0.1,0,0,timer_fn,&tc);
    if(!a.timer){a.result=1;goto done;}
    CFRunLoopAddTimer(CFRunLoopGetCurrent(),a.timer,kCFRunLoopCommonModes);
    CFRunLoopRun();
done:
    if(a.timer){CFRunLoopTimerInvalidate(a.timer);CFRelease(a.timer);}
    if(cleanup(&a))a.result=1;
    if(a.notifier)IODeregisterForSystemPower(&a.notifier);
    if(a.power)IOServiceClose(a.power);
    if(a.notify)IONotificationPortDestroy(a.notify);
    if(a.mac.trace)fclose(a.mac.trace);
    close(lockfd);return a.result;
}
#else
#include <stdio.h>
int main(void){fputs("This executable uses macOS IOKit; build it on an Intel Mac. No Linux RM backend is provided.\n",stderr);return 1;}
#endif

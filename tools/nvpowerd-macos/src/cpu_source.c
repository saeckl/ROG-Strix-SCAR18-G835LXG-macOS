/* SPDX-License-Identifier: MIT
 * Read-only Intel package energy adapter. No kernel extension, CPU setter,
 * shell, sensor-name guess, or NVIDIA power measurement is used here. */
#include "cpu_source.h"
#include <math.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
int nv_cpu_energy(double joules,double ns,uint32_t *mw,uint32_t *interval){
    if(!isfinite(joules)||!isfinite(ns)||joules<=0 || ns<75000000 || ns>250000000)return EINVAL;
    double v=joules*1e12/ns;
    if(!isfinite(v)||v<=0||v>500000)return EINVAL;
    *mw=(uint32_t)v;if((double)*mw<v)(*mw)++; /* round package draw up */
    *interval=(uint32_t)(ns/1e6);return 0;
}
#if defined(__APPLE__) || defined(NVPOWERD_TEST_CPU_API)
#ifdef NVPOWERD_TEST_CPU_API
#include "cpu_cf_mock.h"
#else
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
extern char **environ;
#define FRAME_CAP (1024u*1024u)
static uint64_t monotonic_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
static bool number(CFDictionaryRef d,CFStringRef key,double *v){
    CFTypeRef x=CFDictionaryGetValue(d,key);
    return x && CFGetTypeID(x)==CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)x,kCFNumberDoubleType,v);
}
static int parse_frame(NvCpuSource *s,uint64_t now){
    CFDataRef data=CFDataCreate(NULL,s->frame,(CFIndex)s->used);if(!data)return ENOMEM;
    CFPropertyListRef p=CFPropertyListCreateWithData(NULL,data,kCFPropertyListImmutable,NULL,NULL);CFRelease(data);
    int result=EINVAL;double dbg_age=-1,dbg_j=-1,dbg_el=-1;
    if(p && CFGetTypeID(p)==CFDictionaryGetTypeID()){
        CFDictionaryRef root=(CFDictionaryRef)p;
        CFTypeRef proc=CFDictionaryGetValue(root,CFSTR("processor"));
        CFTypeRef delta=CFDictionaryGetValue(root,CFSTR("is_delta"));
        CFTypeRef stamp=CFDictionaryGetValue(root,CFSTR("timestamp"));
        double joules=-1,elapsed=-1;
        /* XML dates can have one-second precision. Transport freshness is
           enforced separately by nonblocking polls and backlog rejection. */
        double age=stamp && CFGetTypeID(stamp)==CFDateGetTypeID()?
            CFAbsoluteTimeGetCurrent()-CFDateGetAbsoluteTime((CFDateRef)stamp):1e9;
        if(proc && CFGetTypeID(proc)==CFDictionaryGetTypeID() &&
           delta && CFGetTypeID(delta)==CFBooleanGetTypeID() && CFBooleanGetValue((CFBooleanRef)delta) &&
           isfinite(age) && age>=-0.1 && age<=1.25 &&
           number(root,CFSTR("elapsed_ns"),&elapsed) &&
           number((CFDictionaryRef)proc,CFSTR("package_joules"),&joules)){
            dbg_age=age;
            result=nv_cpu_energy(joules,elapsed,&s->mw,&s->interval_ms);
            dbg_j=joules;dbg_el=elapsed;
            if(!result){s->seq++;s->last_ns=now;
                if(s->trace){fprintf(s->trace,"cpu_sample seq=%llu mono_ns=%llu package_joules=%.9g elapsed_ns=%.9g package_mW=%u source=powermetrics.processor.package_joules\n",(unsigned long long)s->seq,(unsigned long long)now,joules,elapsed,s->mw);fflush(s->trace);}
            }
        }
    }
    if(p)CFRelease(p);
    if(result && s->trace){fprintf(s->trace,"cpu_sample rejected: schema/interval/power/timestamp age=%.3f joules=%.6g elapsed_ns=%.6g\n",dbg_age,dbg_j,dbg_el);fflush(s->trace);}
    return result;
}
int nv_cpu_start(NvCpuSource *s,FILE *trace){
    if(s->started)return s->failed?EIO:0;
    memset(s,0,sizeof(*s));s->pid=s->fd=-1;s->trace=trace;
    s->frame=malloc(FRAME_CAP);if(!s->frame)return ENOMEM;
    int fds[2];if(pipe(fds)){int e=errno;free(s->frame);s->frame=NULL;return e;}
    if(fcntl(fds[0],F_SETFL,O_NONBLOCK)||fcntl(fds[0],F_SETFD,FD_CLOEXEC)||fcntl(fds[1],F_SETFD,FD_CLOEXEC)){
        int e=errno;close(fds[0]);close(fds[1]);free(s->frame);s->frame=NULL;return e;
    }
    posix_spawn_file_actions_t actions;
    int e=posix_spawn_file_actions_init(&actions);
    if(!e){
        e=posix_spawn_file_actions_adddup2(&actions,fds[1],STDOUT_FILENO);
        if(!e)e=posix_spawn_file_actions_addclose(&actions,fds[0]);
        if(!e)e=posix_spawn_file_actions_addclose(&actions,fds[1]);
        char *args[]={"/usr/bin/powermetrics","--samplers","cpu_power","-i","100","-f","plist","-b","0",NULL};
        pid_t child=0;if(!e)e=posix_spawn(&child,args[0],&actions,NULL,args,environ);
        if(!e)s->pid=(int)child;
        posix_spawn_file_actions_destroy(&actions);
    }
    close(fds[1]);
    if(e){close(fds[0]);free(s->frame);s->frame=NULL;return e;}
    s->fd=fds[0];s->started=true;s->poll_ns=monotonic_ns();
    if(trace){fprintf(trace,"cpu_source pid=%d interval=100ms command=/usr/bin/powermetrics --samplers cpu_power -i 100 -f plist -b 0\n",s->pid);fflush(trace);}
    return 0;
}
int nv_cpu_read(NvCpuSource *s,NvCpuSample *out){
    memset(out,0,sizeof(*out));if(!s->started||s->failed)return ENODATA;
    uint64_t now=monotonic_ns();if(!now||now<s->poll_ns){s->failed=true;return EIO;}
    bool gap=now-s->poll_ns>250000000u;s->poll_ns=now;
    if(gap)s->used=0;
    unsigned frames=0,bytes=0;bool rejected=false,drained=false;unsigned char buf[8192];
    for(unsigned batch=0;batch<64;batch++){
        ssize_t n=read(s->fd,buf,sizeof(buf));
        if(n<0 && errno==EINTR)continue;
        if(n<0 && (errno==EAGAIN||errno==EWOULDBLOCK)){drained=true;break;}
        if(n<=0){s->failed=true;return EPIPE;}
        bytes+=(unsigned)n;
        for(ssize_t i=0;i<n;i++){
            if(buf[i]==0){
                if(s->used){frames++;
                    if(gap || frames>2){rejected=true;if(s->trace){fprintf(s->trace,"cpu_frame rejected: gap=%d frames=%u\n",gap,frames);fflush(s->trace);}}
                    else if(parse_frame(s,now))rejected=true;}
                s->used=0;
            }else{
                if(s->used>=FRAME_CAP){s->failed=true;return EOVERFLOW;}
                s->frame[s->used++]=buf[i];
            }
        }
    }
    (void)bytes;
    if(!drained || gap || frames>2 || rejected){s->last_ns=0;if(gap)s->used=0;return ENODATA;}
    if(!s->last_ns || now<s->last_ns || now-s->last_ns>350000000u)return ENODATA;
    *out=(NvCpuSample){true,s->mw,(uint32_t)((now-s->last_ns)/1000000u),s->interval_ms,s->seq};return 0;
}
static pid_t wait_owned(pid_t pid,int *status,int flags){
    pid_t r;do{r=waitpid(pid,status,flags);}while(r<0 && errno==EINTR);return r;
}
int nv_cpu_stop(NvCpuSource *s){
    if(!s->started)return 0;
    int bad=0,status=0;pid_t pid=(pid_t)s->pid;
    if(pid>0){
        pid_t r=wait_owned(pid,&status,WNOHANG);
        if(r==0){
            if(kill(pid,SIGTERM)&&errno!=ESRCH)bad=EIO;
            for(unsigned i=0;i<20 && r==0;i++){struct timespec delay={0,10000000};(void)nanosleep(&delay,NULL);r=wait_owned(pid,&status,WNOHANG);}
            if(r==0){if(kill(pid,SIGKILL)&&errno!=ESRCH)bad=EIO;r=wait_owned(pid,&status,0);}
        }
        if(r<0 && errno!=ECHILD)bad=EIO;
    }
    if(s->fd>=0 && close(s->fd))bad=errno;
    if(s->trace){fprintf(s->trace,"cpu_source stopped pid=%d cleanup_errno=%d\n",s->pid,bad);fflush(s->trace);}
    free(s->frame);memset(s,0,sizeof(*s));s->fd=s->pid=-1;return bad;
}
#else
int nv_cpu_start(NvCpuSource *s,FILE *f){(void)s;(void)f;return ENOTSUP;}
int nv_cpu_read(NvCpuSource *s,NvCpuSample *out){(void)s;memset(out,0,sizeof(*out));return ENOTSUP;}
int nv_cpu_stop(NvCpuSource *s){(void)s;return 0;}
#endif

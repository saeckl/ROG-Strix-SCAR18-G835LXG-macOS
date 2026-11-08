/* SPDX-License-Identifier: MIT
 * scarbench – small, portable CPU/RAM benchmark for comparing macOS, Linux and Windows
 * on the same machine. One file, no dependencies.
 *
 *   macOS : clang -O2 -o scarbench scarbench.c -lpthread
 *   Linux : gcc   -O2 -o scarbench scarbench.c -lpthread
 *   Windows (MSVC "x64 Native Tools" prompt): cl /O2 scarbench.c
 *   Windows (MinGW / clang):                  gcc -O2 -o scarbench.exe scarbench.c
 *
 * Tests
 *   clock    dependent 64-bit reg-reg add chain (1 cycle per add on Intel P and E cores)
 *            -> effective core clock in GHz, single thread and per thread with all threads busy
 *   fp       independent FP64 multiply-add chains per thread (scalar, compiler may vectorise)
 *   int      integer hash mixing per thread (xorshift/multiply), typical for compression-like code
 *   triad    STREAM triad a = b + s*c over 3 x 512 MiB, all threads, GB/s
 *   copy     STREAM copy, all threads, GB/s
 *   latency  random pointer chase in 1 GiB, ns per load, single thread
 *
 *   scarbench sustain [seconds] [threads]
 *            all threads run the add chain for N seconds (default 60) and the mean core
 *            clock is printed every 5 s -> shows PL2 -> PL1 power-limit behaviour
 * Each test runs several times; the best run is reported (median also printed for clock).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE thr_t;
static double now(void){LARGE_INTEGER f,c;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&c);return (double)c.QuadPart/(double)f.QuadPart;}
static int ncpu(void){return (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);}
typedef struct {void *(*fn)(void*);void *arg;} tramp_t;
static DWORD WINAPI tramp(LPVOID p){tramp_t *t=p;t->fn(t->arg);free(t);return 0;}
static int thr_start(thr_t *t,void *(*fn)(void*),void *arg){tramp_t *x=malloc(sizeof *x);x->fn=fn;x->arg=arg;*t=CreateThread(NULL,0,tramp,x,0,NULL);return *t?0:-1;}
static void thr_join(thr_t t){WaitForSingleObject(t,INFINITE);CloseHandle(t);}
static void *big_alloc(size_t n){return VirtualAlloc(NULL,n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);}
static void big_free(void *p,size_t n){(void)n;VirtualFree(p,0,MEM_RELEASE);}
#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
typedef pthread_t thr_t;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int ncpu(void){long n=sysconf(_SC_NPROCESSORS_ONLN);return n>0?(int)n:1;}
static int thr_start(thr_t *t,void *(*fn)(void*),void *arg){return pthread_create(t,NULL,fn,arg);}
static void thr_join(thr_t t){pthread_join(t,NULL);}
static void *big_alloc(size_t n){void *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);return p==MAP_FAILED?NULL:p;}
static void big_free(void *p,size_t n){munmap(p,n);}
#endif

/* ---------- start barrier so all threads run at the same time ---------- */
static volatile int go_flag;
static volatile int ready_count;
#if defined(_MSC_VER)
#define ATOMIC_INC(p) InterlockedIncrement((volatile LONG*)(p))
#define PAUSE() YieldProcessor()
#else
#define ATOMIC_INC(p) __sync_fetch_and_add((p),1)
#define PAUSE() __asm__ __volatile__("pause")
#endif
static void wait_go(void){ATOMIC_INC(&ready_count);while(!go_flag)PAUSE();}

/* ---------- clock: dependent add chain ---------- */
static uint64_t add_chain(uint64_t n){
    uint64_t x=0;
#if defined(__x86_64__) && !defined(_MSC_VER)
    /* register operand: newer Intel cores fold dependent add-immediate chains in the renamer */
    volatile uint64_t yv=3; uint64_t y=yv;
    for(uint64_t i=0;i<n;i++){
        __asm__ __volatile__(
            "add %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\t"
            "add %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\t"
            "add %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\t"
            "add %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\t"
            "add %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\t"
            "add %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\t"
            "add %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\t"
            "add %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\tadd %1,%0\n\t"
            : "+r"(x) : "r"(y));
    }
#else
    /* MSVC has no x64 inline asm: a data-dependent chain the optimiser cannot fold.
       Two dependent ops per step (add + xor), so the GHz estimate divides by 2. */
    volatile uint64_t seed=1; uint64_t k=seed;
    for(uint64_t i=0;i<n*32;i++){x+=k;k^=x;}
    if(x==42)printf("%llu",(unsigned long long)k);
#endif
    return x;
}
#if defined(__x86_64__) && !defined(_MSC_VER)
#define ADDS_PER_ITER 64.0
#else
#define ADDS_PER_ITER 64.0 /* 32 iterations x 2 dependent ops */
#endif

typedef struct {int id; uint64_t n; double sec; double result; void *mem; size_t len; size_t lo,hi; double s;} job_t;

static void *clock_job(void *p){job_t *j=p;wait_go();double t=now();volatile uint64_t r=add_chain(j->n);(void)r;j->sec=now()-t;return NULL;}

static uint64_t mix(uint64_t x){x^=x>>33;x*=0xff51afd7ed558ccdULL;x^=x>>33;x*=0xc4ceb9fe1a85ec53ULL;x^=x>>33;return x;}
static void *int_job(void *p){job_t *j=p;wait_go();double t=now();uint64_t a=j->id+1,b=7,c=13,d=99;
    for(uint64_t i=0;i<j->n;i++){a=mix(a+i);b=mix(b^a);c=mix(c+b);d=mix(d^c);}
    j->sec=now()-t;j->result=(double)((a^b^c^d)&1);return NULL;}
static void *fp_job(void *p){job_t *j=p;wait_go();double t=now();
    double a0=1.0+j->id*1e-9,a1=a0+1e-9,a2=a0+2e-9,a3=a0+3e-9,a4=a0+4e-9,a5=a0+5e-9,a6=a0+6e-9,a7=a0+7e-9;
    const double m=0.9999999,c=1e-7;
    for(uint64_t i=0;i<j->n;i++){a0=a0*m+c;a1=a1*m+c;a2=a2*m+c;a3=a3*m+c;a4=a4*m+c;a5=a5*m+c;a6=a6*m+c;a7=a7*m+c;}
    j->sec=now()-t;j->result=a0+a1+a2+a3+a4+a5+a6+a7;return NULL;}

static double *A,*B,*C;
static void *triad_job(void *p){job_t *j=p;wait_go();double t=now();double s=j->s;
    for(size_t i=j->lo;i<j->hi;i++)A[i]=B[i]+s*C[i];j->sec=now()-t;return NULL;}
static void *copy_job(void *p){job_t *j=p;wait_go();double t=now();
    memcpy(A+j->lo,B+j->lo,(j->hi-j->lo)*sizeof(double));j->sec=now()-t;return NULL;}
static void *init_job(void *p){job_t *j=p;wait_go();for(size_t i=j->lo;i<j->hi;i++){A[i]=0;B[i]=1;C[i]=2;}return NULL;}

/* run `threads` copies of fn, return wall time of the slowest */
static double run(int threads,void *(*fn)(void*),job_t *jobs){
    thr_t *t=malloc(sizeof(thr_t)*threads);go_flag=0;ready_count=0;
    for(int i=0;i<threads;i++)thr_start(&t[i],fn,&jobs[i]);
    while(ready_count<threads)PAUSE();
    double t0=now();go_flag=1;
    for(int i=0;i<threads;i++)thr_join(t[i]);
    double wall=now()-t0;free(t);return wall;
}
static int cmpd(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return x<y?-1:x>y;}


/* ---------- sustain mode ---------- */
static volatile int stop_flag;
static volatile uint64_t *progress;
static void *sustain_job(void *p){job_t *j=p;wait_go();uint64_t done=0;
    while(!stop_flag){add_chain(1000000ull);done++;progress[j->id*8]=done;}return NULL;}
static int sustain(int seconds,int threads){
    job_t *jobs=calloc(threads,sizeof(job_t));thr_t *t=malloc(sizeof(thr_t)*threads);
    progress=calloc((size_t)threads*8,sizeof(uint64_t));
    printf("scarbench 1.0 sustain %d s, threads=%d (mean core GHz per 5 s)\n",seconds,threads);
    go_flag=0;ready_count=0;stop_flag=0;
    for(int i=0;i<threads;i++){jobs[i].id=i;thr_start(&t[i],sustain_job,&jobs[i]);}
    while(ready_count<threads)PAUSE();
    go_flag=1;double t0=now(),last=t0;uint64_t prev=0;
    for(int k=5;k<=seconds;k+=5){
        while(now()-t0<k){
#ifdef _WIN32
            Sleep(50);
#else
            usleep(50000);
#endif
        }
        uint64_t sum=0;for(int i=0;i<threads;i++)sum+=progress[i*8];
        double tn=now();
        printf("t=%3d s  mean %.2f GHz\n",k,(double)(sum-prev)*1000000.0*ADDS_PER_ITER/(tn-last)/threads/1e9);fflush(stdout);
        prev=sum;last=tn;
    }
    stop_flag=1;for(int i=0;i<threads;i++)thr_join(t[i]);
    free(jobs);free(t);free((void*)progress);return 0;
}
int main(int argc,char **argv){
    int threads=ncpu();
    if(argc>1 && !strcmp(argv[1],"sustain"))
        return sustain(argc>2?atoi(argv[2]):60,argc>3?atoi(argv[3]):threads);
    if(argc>1)threads=atoi(argv[1]);
    if(threads<1)threads=1;
    job_t *jobs=calloc(threads,sizeof(job_t));
    printf("scarbench 1.0  threads=%d\n",threads);

    /* clock single: 1e8 iters * 64 adds = 6.4e9 adds (~1.3 s at 5 GHz) */
    {double g[5];for(int r=0;r<5;r++){job_t j={0};j.n=100000000ull/ (r==0?4:1);run(1,clock_job,&j);if(r==0){g[r]=0;continue;}g[r]=j.n*ADDS_PER_ITER/j.sec/1e9;}
     qsort(g+1,4,sizeof(double),cmpd);printf("clock_single_GHz      best %.2f  median %.2f\n",g[4],(g[2]+g[3])/2);}
    /* clock all threads: per-thread GHz, sorted -> shows P vs E cores and multi-core clock drop */
    {for(int i=0;i<threads;i++){jobs[i]=(job_t){0};jobs[i].n=60000000ull;}
     run(threads,clock_job,jobs);
     double *g=malloc(sizeof(double)*threads);double sum=0;
     for(int i=0;i<threads;i++){g[i]=jobs[i].n*ADDS_PER_ITER/jobs[i].sec/1e9;sum+=g[i];}
     qsort(g,threads,sizeof(double),cmpd);
     printf("clock_allcore_GHz     min %.2f  median %.2f  max %.2f  sum %.1f\n",g[0],g[threads/2],g[threads-1],sum);
     printf("clock_allcore_sorted ");for(int i=threads-1;i>=0;i--)printf(" %.2f",g[i]);printf("\n");free(g);}
    /* int + fp: score = Gop/s */
    {double best1=0,bestN=0;
     for(int r=0;r<3;r++){job_t j={0};j.n=40000000ull;run(1,int_job,&j);double v=j.n*4/j.sec/1e9;if(v>best1)best1=v;}
     for(int r=0;r<2;r++){for(int i=0;i<threads;i++){jobs[i]=(job_t){0};jobs[i].id=i;jobs[i].n=40000000ull;}
        double w=run(threads,int_job,jobs);double v=threads*40000000.0*4/w/1e9;if(v>bestN)bestN=v;}
     printf("int_mix_Gops          1T %.2f  nT %.1f  scaling %.1fx\n",best1,bestN,bestN/best1);}
    {double best1=0,bestN=0;
     for(int r=0;r<3;r++){job_t j={0};j.n=200000000ull;run(1,fp_job,&j);double v=j.n*16/j.sec/1e9;if(v>best1)best1=v;}
     for(int r=0;r<2;r++){for(int i=0;i<threads;i++){jobs[i]=(job_t){0};jobs[i].id=i;jobs[i].n=200000000ull;}
        double w=run(threads,fp_job,jobs);double v=threads*200000000.0*16/w/1e9;if(v>bestN)bestN=v;}
     printf("fp64_GFLOPS           1T %.1f  nT %.1f  scaling %.1fx\n",best1,bestN,bestN/best1);}
    /* memory bandwidth: 3 x 512 MiB */
    {size_t n=64ull*1024*1024,bytes=n*sizeof(double);
     A=big_alloc(bytes);B=big_alloc(bytes);C=big_alloc(bytes);
     if(!A||!B||!C){printf("alloc failed\n");return 1;}
     for(int i=0;i<threads;i++){jobs[i]=(job_t){0};jobs[i].lo=n*i/threads;jobs[i].hi=n*(i+1)/threads;jobs[i].s=3.0;}
     run(threads,init_job,jobs);
     double bt=0,bc=0,b1=0;
     for(int r=0;r<5;r++){double w=run(threads,triad_job,jobs);double v=3.0*bytes/w/1e9;if(v>bt)bt=v;}
     for(int r=0;r<5;r++){double w=run(threads,copy_job,jobs);double v=2.0*bytes/w/1e9;if(v>bc)bc=v;}
     for(int r=0;r<3;r++){job_t j={0};j.lo=0;j.hi=n;j.s=3.0;double w=run(1,triad_job,&j);double v=3.0*bytes/w/1e9;if(v>b1)b1=v;}
     printf("mem_triad_GBps        nT %.1f  1T %.1f\n",bt,b1);
     printf("mem_copy_GBps         nT %.1f\n",bc);
     big_free(A,bytes);big_free(B,bytes);big_free(C,bytes);}
    /* latency: random cyclic permutation in 1 GiB of 64-byte slots */
    {size_t slots=(1ull<<30)/64,bytes=slots*64;char *m=big_alloc(bytes);if(!m){printf("alloc failed\n");return 1;}
     uint32_t *perm=malloc(sizeof(uint32_t)*slots);for(size_t i=0;i<slots;i++)perm[i]=(uint32_t)i;
     uint64_t rs=88172645463325252ull;
     for(size_t i=slots-1;i>0;i--){rs^=rs<<13;rs^=rs>>7;rs^=rs<<17;size_t k=rs%(i+1);uint32_t tmp=perm[i];perm[i]=perm[k];perm[k]=tmp;}
     for(size_t i=0;i<slots;i++)*(void**)(m+(size_t)perm[i]*64)=m+(size_t)perm[(i+1)%slots]*64;
     free(perm);
     double best=1e9;
     for(int r=0;r<3;r++){void *p=m;size_t steps=20000000;double t=now();for(size_t i=0;i<steps;i++)p=*(void**)p;double ns=(now()-t)/steps*1e9;if(ns<best)best=ns;if(p==NULL)printf("x");}
     printf("mem_latency_ns        %.1f\n",best);big_free(m,bytes);}
    free(jobs);return 0;
}

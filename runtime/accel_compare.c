/*
 * Phase 3.3 headline experiment: "what should the CPU do while waiting for the
 * accelerator?" Three approaches on ONE server core, against an emulated
 * accelerator (separate core) with a controllable offload latency L:
 *
 *   busy   - busy-wait until the offload completes (no overlap)            [notes approach 2]
 *   block  - relinquish the core; OS context-switches to another thread    [notes approach 1]
 *   coro   - transparent coroutine: park at offload, run another handler   [ours]
 *
 * Request = preprocess(Wpre ns CPU) -> offload(L ns on device) -> postprocess(Wpost ns CPU).
 * C concurrent connections. We sweep L and report throughput + latency.
 *
 * Expected: busy throughput ~ 1/(W+L) (collapses with L); block ~ 1/(W+ctxsw)
 * (pays the OS switch per offload); coro ~ 1/(W+tiny) (hides L entirely).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <time.h>
#include "fw_fiber.h"   /* FastWake's ~6ns user-level context switch (no sigprocmask) */

static inline uint64_t now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000ull+t.tv_nsec; }
static inline void cpu_relax(void){ __asm__ __volatile__("pause"); }
/* burn approximately ns of CPU (models pre/post-processing work) */
static void spin_ns(uint64_t ns){ uint64_t s=now_ns(); while(now_ns()-s < ns) cpu_relax(); }

static void pin(int core){ cpu_set_t s; CPU_ZERO(&s); CPU_SET(core,&s); sched_setaffinity(0,sizeof s,&s); }
/* SCHED_FIFO so the polling device/server threads aren't descheduled by co-tenant
   load on these non-isolated cores (needs CAP_SYS_NICE; run under sudo). */
static void set_rt(int prio){ struct sched_param p; p.sched_priority=prio; sched_setscheduler(0,SCHED_FIFO,&p); }

/* ---------------- emulated accelerator (its own core) ---------------- */
#define MAXSLOT 512
struct slot { volatile uint64_t deadline; volatile int active; volatile int done; int efd; };
static struct slot slots[MAXSLOT];
static volatile int dev_run = 1;
static int DEV_CORE = 4, SRV_CORE = 2;

static void* device_fn(void* a){
    (void)a; pin(DEV_CORE); set_rt(90);
    while(dev_run){
        uint64_t t = now_ns();
        for(int i=0;i<MAXSLOT;i++){
            if(slots[i].active && !slots[i].done && t>=slots[i].deadline){
                slots[i].done = 1;
                if(slots[i].efd>=0){ uint64_t one=1; if(write(slots[i].efd,&one,8)){} }
            }
        }
    }
    return 0;
}
static uint64_t JIT_NS=0; static uint64_t lcg=0x243F6A8885A308D3ull;
static inline uint64_t jitter(void){ if(!JIT_NS) return 0; lcg=lcg*6364136223846793005ull+1442695040888963407ull; return (lcg>>33)%JIT_NS; }
static inline void submit(int slotid, uint64_t L){ slots[slotid].done=0; slots[slotid].deadline=now_ns()+L+jitter(); slots[slotid].active=1; }

/* ---------------- workload params + measurement ---------------- */
static uint64_t L_NS=20000, WPRE=1000, WPOST=1000, RUN_NS=1500000000ull;
static int C=64;
static volatile int stop=0;
/* latency samples (sampled) */
#define MAXSAMP 300000
static uint64_t lat[3][MAXSAMP]; static volatile long nsamp[3]; static volatile long ndone[3];
static int MODEIDX;
static inline void record(uint64_t start){
    long d=__sync_fetch_and_add(&ndone[MODEIDX],1);
    long s=nsamp[MODEIDX];
    if((d & 7)==0 && s<MAXSAMP){ lat[MODEIDX][s]=now_ns()-start; nsamp[MODEIDX]=s+1; }
}

/* ---------------- busy-wait mode (single thread) ---------------- */
static void run_busy(void){
    pin(SRV_CORE); set_rt(80); slots[0].efd=-1;
    while(!stop){
        uint64_t t0=now_ns();
        spin_ns(WPRE);
        submit(0,L_NS);
        while(!slots[0].done) cpu_relax();         /* busy wait, no overlap */
        spin_ns(WPOST);
        record(t0);
    }
}

/* ---------------- block mode (C threads, OS context switch) ---------------- */
static void* block_worker(void* arg){
    int id=(int)(long)arg; pin(SRV_CORE); set_rt(80);
    int efd=eventfd(0,0); slots[id].efd=efd;
    while(!stop){
        uint64_t t0=now_ns();
        spin_ns(WPRE);
        submit(id,L_NS);
        uint64_t v; if(read(efd,&v,8)){}             /* block -> OS switches to another thread */
        spin_ns(WPOST);
        record(t0);
    }
    close(efd); return 0;
}
static void run_block(void){
    pthread_t th[MAXSLOT];
    for(long i=0;i<C;i++) pthread_create(&th[i],0,block_worker,(void*)i);
    for(int i=0;i<C;i++) pthread_join(th[i],0);
}

/* ---------------- coroutine mode (1 thread, C FastWake fibers) ---------------- */
struct coro { fw_fiber fb; int id; int waiting; int finished; uint64_t start; };
static struct coro coros[MAXSLOT]; static fw_fiber sched_fb; static int cur;
static void coro_offload(void){ coros[cur].waiting=1; submit(cur,L_NS); fw_fiber_switch(&coros[cur].fb,&sched_fb); }
static void coro_body(void* arg){
    int id=(int)(long)arg;
    while(!stop){
        coros[id].start=now_ns();
        spin_ns(WPRE);
        coro_offload();          /* park here; resumed when device completes */
        spin_ns(WPOST);
        record(coros[id].start);
    }
    coros[id].finished=1;
    /* return to scheduler via link */
}
static void run_coro(void){
    pin(SRV_CORE); set_rt(80);
    fw_fiber_init_self(&sched_fb);
    for(int i=0;i<C;i++){ slots[i].efd=-1; slots[i].active=0; slots[i].done=0;
        coros[i].id=i; coros[i].waiting=0; coros[i].finished=0;
        fw_fiber_make(&coros[i].fb, 64*1024, coro_body, (void*)(long)i, &sched_fb);
    }
    /* dispatcher: poll device completions, then run every runnable coro one step */
    for(;;){
        for(int i=0;i<C;i++) if(coros[i].waiting && slots[i].done){ coros[i].waiting=0; slots[i].active=0; }
        int ran=0, parked=0, fin=0;
        for(int i=0;i<C;i++){
            if(coros[i].finished){ fin++; continue; }
            if(coros[i].waiting){ parked++; continue; }
            cur=i; fw_fiber_switch(&sched_fb,&coros[i].fb); ran++;
        }
        if(fin==C) break;
        if(ran==0 && parked>0) cpu_relax();   /* all parked: poll device until one completes */
    }
}

static int cmp(const void*a,const void*b){ uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b; return x<y?-1:x>y?1:0; }

static void* timer_fn(void* a){ (void)a; uint64_t s=now_ns(); while(now_ns()-s<RUN_NS && !stop) usleep(2000); stop=1; return 0; }

int main(int argc,char**argv){
    const char* mode = argc>1?argv[1]:"coro";
    if(argc>2) L_NS=strtoull(argv[2],0,10);
    if(argc>3) C=atoi(argv[3]);
    if(argc>4) WPRE=WPOST=strtoull(argv[4],0,10)/2;
    if(argc>5) RUN_NS=strtoull(argv[5],0,10)*1000000ull;   /* run_ms */
    if(argc>6) JIT_NS=strtoull(argv[6],0,10);              /* jitter ns */
    if(C>MAXSLOT) C=MAXSLOT;
    MODEIDX = !strcmp(mode,"busy")?0 : !strcmp(mode,"block")?1 : 2;
    if(!strcmp(mode,"busy")) C=1;

    pthread_t dev; pthread_create(&dev,0,device_fn,0);
    pthread_t tmr; pthread_create(&tmr,0,timer_fn,0);
    usleep(50000);  /* let device spin up */

    uint64_t t0=now_ns();
    if(!strcmp(mode,"busy")) run_busy();
    else if(!strcmp(mode,"block")) run_block();
    else run_coro();
    uint64_t dt=now_ns()-t0;

    dev_run=0; pthread_join(dev,0); pthread_join(tmr,0);

    long done=ndone[MODEIDX]; long ns=nsamp[MODEIDX];
    qsort(lat[MODEIDX],ns,sizeof(uint64_t),cmp);
    double tput = done/((double)dt/1e9);
    uint64_t p50= ns? lat[MODEIDX][ns/2]:0, p99= ns? lat[MODEIDX][(long)(ns*0.99)]:0, p999= ns? lat[MODEIDX][(long)(ns*0.999)]:0;
    // mode,L_us,C,W_us,tput_reqps,p50_us,p99_us,p999_us
    printf("%s,%.1f,%d,%.1f,%.0f,%.1f,%.1f,%.1f\n",
        mode, L_NS/1000.0, C, (WPRE+WPOST)/1000.0, tput, p50/1000.0, p99/1000.0, p999/1000.0);
    return 0;
}

/*
 * Phase 4: speculate-or-serialize conflict detector for transparent offload.
 *
 * The hazard (taxonomy "cache" pattern, cross-fd shared state): a handler reads a
 * shared entry, offloads (PARKS at the accelerator), then commits a read-modify-write
 * based on what it read. While parked, another coroutine may modify the same entry.
 *   naive  : commit unconditionally -> LOST UPDATES (incorrect).
 *   detect : at commit, validate the entry's version vs what was read; on mismatch
 *            SERIALIZE (re-read current value and re-apply = always correct). This is
 *            the "speculate, but fall back to serialize on conflict" rule — sound
 *            because the fallback is always safe.
 *   serial : fd-partition/lock baseline — never speculate across the shared entry.
 *
 * We measure: correctness (lost updates), throughput/overhead, and fallback rate as a
 * function of contention (number of distinct shared keys).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <time.h>
#include "fw_fiber.h"

static inline uint64_t now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000ull+t.tv_nsec; }
static void pin(int c){ cpu_set_t s; CPU_ZERO(&s); CPU_SET(c,&s); sched_setaffinity(0,sizeof s,&s); }
static void set_rt(int p){ struct sched_param q; q.sched_priority=p; sched_setscheduler(0,SCHED_FIFO,&q); }

/* emulated accelerator */
#define MAXSLOT 512
struct slot { volatile uint64_t deadline; volatile int active, done; };
static struct slot slots[MAXSLOT]; static volatile int dev_run=1; static uint64_t L_NS=20000;
static void* device_fn(void* a){ (void)a; pin(4); set_rt(90);
    while(dev_run){ uint64_t t=now_ns(); for(int i=0;i<MAXSLOT;i++) if(slots[i].active&&!slots[i].done&&t>=slots[i].deadline) slots[i].done=1; }
    return 0; }
static inline void submit(int s){ slots[s].done=0; slots[s].deadline=now_ns()+L_NS; slots[s].active=1; }

/* shared cache (the cross-connection state under test) */
#define MAXKEY 100000
static struct { volatile long val; volatile uint64_t ver; } cache[MAXKEY];
static volatile int keylock[MAXKEY];   /* per-key lock for the lock-respecting baseline */
static int NKEY=64;                 /* fewer keys -> more contention */
static long g_commits=0, g_fallbacks=0, g_lockwaits=0;

static int MODE=1;                  /* 0 naive, 1 detect, 2 serial */
static int C=64, N_ITERS=20000; static volatile int stop=0;
static uint64_t REDO_NS=0;          /* cost of re-running post-processing on fallback */
static void spin_ns(uint64_t ns){ uint64_t s=now_ns(); while(now_ns()-s<ns) __asm__ __volatile__("pause"); }

struct coro { fw_fiber fb; int id, waiting, finished; };
static struct coro coros[MAXSLOT]; static fw_fiber sched_fb; static int cur;

static void coro_offload(void){ coros[cur].waiting=1; submit(cur); fw_fiber_switch(&coros[cur].fb,&sched_fb); }

static void body(void* arg){
    int id=(int)(long)arg;
    for(int i=0;i<N_ITERS && !stop;i++){
        int k = (id*2654435761u + i*40503u) % NKEY;     /* keys collide across coros */
        if(MODE==3){
            /* LOCK baseline (lock-respecting scheme): hold the per-key lock ACROSS the
               offload, so same-key handlers serialize. Correct, but loses overlap on hot keys.
               A coroutine blocked on a held lock parks and retries (cooperative). */
            while(__sync_lock_test_and_set(&keylock[k],1)){ g_lockwaits++; fw_fiber_switch(&coros[cur].fb,&sched_fb); }
            long v=cache[k].val;
            coro_offload();                    /* yield at offload while holding key k */
            cache[k].val=v+1; cache[k].ver++;  /* dependent post-processing */
            __sync_lock_release(&keylock[k]);
            g_commits++;
        } else if(MODE==2){
            /* serial: do the whole RMW without yielding across the shared entry,
               THEN offload (the safe-but-no-overlap-on-this-section ordering) */
            long v=cache[k].val; cache[k].val=v+1; cache[k].ver++;
            g_commits++;
            coro_offload();
        } else {
            long v=cache[k].val; uint64_t ver=cache[k].ver;   /* speculative read */
            coro_offload();                                   /* PARK at offload */
            if(MODE==1 && cache[k].ver!=ver){                 /* detect: validate */
                v=cache[k].val; g_fallbacks++;                /* conflict -> serialize re-read */
                if(REDO_NS) spin_ns(REDO_NS);                 /* model re-running post-processing */
            }
            cache[k].val=v+1; cache[k].ver++;                 /* commit RMW */
            g_commits++;
        }
    }
    coros[id].finished=1;
}

static void run(void){
    pin(2); set_rt(80); fw_fiber_init_self(&sched_fb);
    for(int i=0;i<C;i++){ slots[i].active=0; coros[i].waiting=0; coros[i].finished=0;
        fw_fiber_make(&coros[i].fb,64*1024,body,(void*)(long)i,&sched_fb); }
    for(;;){
        for(int i=0;i<C;i++) if(coros[i].waiting && slots[i].done){ coros[i].waiting=0; slots[i].active=0; }
        int ran=0,parked=0,fin=0;
        for(int i=0;i<C;i++){ if(coros[i].finished){fin++;continue;} if(coros[i].waiting){parked++;continue;}
            cur=i; fw_fiber_switch(&sched_fb,&coros[i].fb); ran++; }
        if(fin==C) break;
        if(ran==0&&parked>0) __asm__ __volatile__("pause");
    }
}

int main(int argc,char**argv){
    const char* m = argc>1?argv[1]:"detect";
    MODE = !strcmp(m,"naive")?0 : !strcmp(m,"serial")?2 : !strcmp(m,"lock")?3 : 1;
    if(argc>2) NKEY=atoi(argv[2]);
    if(argc>3) C=atoi(argv[3]);
    if(argc>4) N_ITERS=atoi(argv[4]);
    if(argc>5) REDO_NS=strtoull(argv[5],0,10);
    if(NKEY>MAXKEY) NKEY=MAXKEY;
    memset(cache,0,sizeof(cache[0])*(NKEY));
    pthread_t dev; pthread_create(&dev,0,device_fn,0); usleep(50000);
    uint64_t t0=now_ns();
    run();
    double dt=(now_ns()-t0)/1e9;
    dev_run=0; pthread_join(dev,0);

    /* correctness oracle: sum of cache == total commits iff no lost updates */
    long sum=0; for(int k=0;k<NKEY;k++) sum+=cache[k].val;
    long lost = g_commits - sum;
    printf("%s,NKEY=%d,C=%d,commits=%ld,sum=%ld,lost_updates=%ld,fallbacks=%ld,fallback_pct=%.2f,tput_ops_s=%.0f\n",
        m,NKEY,C,g_commits,sum,lost,g_fallbacks,100.0*g_fallbacks/(g_commits?g_commits:1),g_commits/dt);
    return 0;
}

/*
 * Phase 3: OPEN-LOOP latency-vs-load. Requests arrive at Poisson rate R independent of
 * completions (vs the closed-loop Pareto). A producer injects arrivals; the system serves
 * them; we measure end-to-end latency = completion - arrival at each offered rate.
 *
 *   coro  - pool of fibers on one core; offload yields; dispatcher assigns arrivals to free
 *           fibers and polls offload completions.
 *   block - pool of T OS threads; each pops an arrival and does work + blocking offload.
 *
 * Shows coro sustains low latency to a higher offered load before the latency knee.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <time.h>
#include "fw_fiber.h"

static inline uint64_t now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000ull+t.tv_nsec; }
static void pin(int c){ cpu_set_t s; CPU_ZERO(&s); CPU_SET(c,&s); sched_setaffinity(0,sizeof s,&s); }
static void set_rt(int p){ struct sched_param q; q.sched_priority=p; sched_setscheduler(0,SCHED_FIFO,&q); }
static void spin_ns(uint64_t ns){ uint64_t s=now_ns(); while(now_ns()-s<ns) __asm__ __volatile__("pause"); }

#define POOL 1024
#define RING 65536
static uint64_t L_NS=20000, WPRE=1000, WPOST=1000; static int RUN_MS=2000;
static volatile int stop=0;

/* emulated accelerator */
struct slot { volatile uint64_t deadline; volatile int active, done; };
static struct slot slots[POOL]; static volatile int dev_run=1;
/* completion queue: device -> dispatcher (avoids O(POOL) scan for completions) */
#define CQN (POOL*4)
static volatile int comp_ring[CQN]; static volatile long comp_head=0, comp_tail=0;
static void* device_fn(void*a){ (void)a; pin(6); set_rt(90);
  while(dev_run){ uint64_t t=now_ns(); for(int i=0;i<POOL;i++) if(slots[i].active&&!slots[i].done&&t>=slots[i].deadline){ slots[i].done=1; long h=comp_head; comp_ring[h%CQN]=i; __sync_synchronize(); comp_head=h+1; } }
  return 0; }

/* arrival ring (producer -> server) */
static volatile uint64_t ring[RING]; static volatile long r_head=0, r_tail=0;
static volatile long injected=0, completed=0, dropped=0;
static uint64_t *lat; static volatile long latn=0;
static volatile uint64_t warm_until=0;
static void record(uint64_t arr){ __sync_fetch_and_add(&completed,1); uint64_t n=now_ns(); if(n<warm_until) return; long i=__sync_fetch_and_add(&latn,1); lat[i%300000]=n-arr; }

static double R_PER_S=100000;
static void* producer(void*a){ (void)a; pin(8); set_rt(85);
    uint64_t seed=88172645463325252ull;
    uint64_t t0=now_ns(), next=t0;
    while(!stop){
        uint64_t now=now_ns();
        if(now>=next){
            long h=r_head, t=r_tail;
            if(h-t < RING){ ring[h%RING]=now; __sync_synchronize(); r_head=h+1; __sync_fetch_and_add(&injected,1); }
            else __sync_fetch_and_add(&dropped,1);
            /* exponential inter-arrival */
            seed^=seed<<13; seed^=seed>>7; seed^=seed<<17;
            double u=((seed>>11)+1)/(double)(1ull<<53);
            double gap = -log(u)/R_PER_S;
            next += (uint64_t)(gap*1e9);
        }
    }
    return 0;
}

/* ---- coro server ---- */
enum { FREE, READY, WAIT_OFF };
struct task { fw_fiber fb; int idx, state; uint64_t arr; };
static struct task tasks[POOL]; static fw_fiber sched_fb; static int cur;
static int freelist[POOL], freen;
static void task_body(void*arg){
    int id=(int)(long)arg;
    for(;;){
        if(stop) return;
        spin_ns(WPRE);
        slots[id].done=0; slots[id].deadline=now_ns()+L_NS; slots[id].active=1;   /* offload */
        tasks[id].state=WAIT_OFF; fw_fiber_switch(&tasks[id].fb,&sched_fb);        /* park */
        if(stop) return;
        spin_ns(WPOST);
        record(tasks[id].arr);
        tasks[id].state=FREE; freelist[freen++]=id;                               /* free self */
        fw_fiber_switch(&tasks[id].fb,&sched_fb);
    }
}
static int ready[POOL*2]; static int rn=0;   /* ready-queue (O(work), not O(POOL)) */
static void* srv_coro(void*p){ (void)p;
    pin(4); set_rt(80); fw_fiber_init_self(&sched_fb); freen=0; rn=0;
    for(int i=0;i<POOL;i++){ tasks[i].idx=i; tasks[i].state=FREE; freelist[freen++]=i;
        fw_fiber_make(&tasks[i].fb,32*1024,task_body,(void*)(long)i,&sched_fb); }
    while(!stop){
        /* assign arrivals to free fibers -> ready-queue */
        while(r_tail<r_head && freen>0){ uint64_t arr=ring[r_tail%RING]; r_tail++;
            int id=freelist[--freen]; tasks[id].arr=arr; tasks[id].state=READY; ready[rn++]=id; }
        /* drain offload completions from the device's queue -> ready (no POOL scan) */
        while(comp_tail<comp_head){ int id=comp_ring[comp_tail%CQN]; comp_tail++;
            if(tasks[id].state==WAIT_OFF){ tasks[id].state=READY; slots[id].active=0; ready[rn++]=id; } }
        /* run all ready fibers one step */
        int m=rn; rn=0; int keep[POOL*2], kn=0;
        for(int j=0;j<m;j++){ int id=ready[j]; if(tasks[id].state!=READY) continue;
            cur=id; fw_fiber_switch(&sched_fb,&tasks[id].fb); }
        (void)keep;(void)kn;
    }
    return 0;
}

/* ---- block server: T threads pop arrivals, blocking offload ---- */
static int NTHREAD=64;
static pthread_mutex_t qmx=PTHREAD_MUTEX_INITIALIZER;
static void* block_worker(void*a){ int id=(int)(long)a; pin(4); set_rt(80);
    while(!stop){
        uint64_t arr=0; int got=0;
        pthread_mutex_lock(&qmx);
        if(r_tail<r_head){ arr=ring[r_tail%RING]; r_tail++; got=1; }
        pthread_mutex_unlock(&qmx);
        if(!got){ struct timespec ts={0,1000}; nanosleep(&ts,0); continue; }
        spin_ns(WPRE);
        slots[id].done=0; slots[id].deadline=now_ns()+L_NS; slots[id].active=1;
        while(!slots[id].done){ struct timespec ts={0,2000}; nanosleep(&ts,0); }   /* block */
        slots[id].active=0; spin_ns(WPOST); record(arr);
    }
    return 0;
}

static int cmp(const void*a,const void*b){ uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b; return x<y?-1:x>y?1:0; }

int main(int argc,char**argv){
    const char*mode=argc>1?argv[1]:"coro";
    if(argc>2) R_PER_S=atof(argv[2]);
    if(argc>3) NTHREAD=atoi(argv[3]);
    lat=malloc(sizeof(uint64_t)*300000);
    pthread_t dev; pthread_create(&dev,0,device_fn,0);
    pthread_t prod; pthread_create(&prod,0,producer,0);
    usleep(50000);
    uint64_t t0=now_ns(); warm_until=t0+400000000ull;
    pthread_t srv, bw[256];
    if(!strcmp(mode,"coro")){ pthread_t s; pthread_create(&s,0,srv_coro,0); srv=s; }
    else { for(int i=0;i<NTHREAD;i++) pthread_create(&bw[i],0,block_worker,(void*)(long)i); }
    usleep(RUN_MS*1000);
    stop=1;
    double dt=(now_ns()-t0)/1e9;
    usleep(100000); dev_run=0;
    long n=latn<300000?latn:300000; qsort(lat,n,sizeof(uint64_t),cmp);
    printf("%s,R=%.0f,offered=%.0f,achieved=%.0f,completed=%ld,dropped=%ld,p50=%.1f,p99=%.1f,p999=%.1f\n",
        mode,R_PER_S, injected/dt, completed/dt, completed, dropped,
        n?lat[n/2]/1000.0:0, n?lat[(long)(n*0.99)]/1000.0:0, n?lat[(long)(n*0.999)]/1000.0:0);
    return 0;
}

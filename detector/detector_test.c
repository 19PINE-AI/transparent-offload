/* Validate the page-protection write-fault + version-snapshot conflict detector on the
 * exact hazard: two cooperative fibers do  v=shared; YIELD(offload); shared=v+1.
 * Detector write-protects the shared region; on a post-offload write to a page modified
 * during the fiber's park, it flags a conflict — WITHOUT any app cooperation. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include "fw_fiber.h"

#define PAGES 16
static long PG;
static unsigned char* region;                 /* the shared state (page-protected) */
static uint64_t gclk=0;                         /* global version clock */
static uint64_t pver[PAGES];                    /* per-page version */
static long conflicts=0;

struct fib { fw_fiber fb; int id; uint64_t park_clk; int parked; int done; };
static struct fib F[2]; static fw_fiber sched; static int cur;

static void protect_all(){ mprotect(region, PAGES*PG, PROT_READ); }
static void seg_handler(int sig, siginfo_t* si, void* uctx){
    (void)sig;(void)uctx;
    uintptr_t a=(uintptr_t)si->si_addr; uintptr_t base=(uintptr_t)region;
    if(a<base || a>=base+PAGES*PG){ _exit(99); }      /* fault outside region: real bug */
    long pg=(a-base)/PG;
    /* a write fault by fiber 'cur' on page pg */
    if(pver[pg] > F[cur].park_clk) conflicts++;        /* page changed during my park -> stale RMW */
    gclk++; pver[pg]=gclk;
    mprotect(region+pg*PG, PG, PROT_READ|PROT_WRITE);  /* let the write proceed */
}

static void park(){ F[cur].parked=1; F[cur].park_clk=gclk; protect_all(); fw_fiber_switch(&F[cur].fb,&sched); }
static long shared_idx_val(int i){ long v; memcpy(&v, region+i*8, 8); return v; }
static void shared_set(int i,long v){ memcpy(region+i*8,&v,8); }

static void body(void* a){
    int id=(int)(long)a;
    long v=shared_idx_val(0);     /* read shared[0] (read allowed; no fault) */
    park();                        /* offload yield */
    shared_set(0, v+1);            /* write shared[0] = read+1  (faults -> detector checks) */
    F[id].done=1;
}

int main(){
    PG=sysconf(_SC_PAGESIZE);
    region=mmap(0,PAGES*PG,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    memset(region,0,PAGES*PG);
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=seg_handler; sa.sa_flags=SA_SIGINFO; sigaction(SIGSEGV,&sa,0);
    fw_fiber_init_self(&sched);
    for(int i=0;i<2;i++){ F[i].id=i; fw_fiber_make(&F[i].fb,64*1024,body,(void*)(long)i,&sched); }
    /* interleave: run A to park, run B to park, resume A (writes), resume B (writes) */
    cur=0; fw_fiber_switch(&sched,&F[0].fb);   /* A: read, park */
    cur=1; fw_fiber_switch(&sched,&F[1].fb);   /* B: read, park */
    protect_all(); cur=0; F[0].parked=0; fw_fiber_switch(&sched,&F[0].fb);  /* A: write (1st writer) */
    protect_all(); cur=1; F[1].parked=0; fw_fiber_switch(&sched,&F[1].fb);  /* B: write (stale -> conflict) */
    mprotect(region,PAGES*PG,PROT_READ|PROT_WRITE);
    printf("shared[0]=%ld (correct RMW would be 2)  conflicts_detected=%ld\n", shared_idx_val(0), conflicts);
    printf("%s\n", conflicts>=1 ? "DETECTOR CAUGHT THE CROSS-FIBER CONFLICT (transparently)" : "MISSED");
    return 0;
}

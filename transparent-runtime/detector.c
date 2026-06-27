#define _GNU_SOURCE
#include "detector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
int det_on=0; long det_conflicts=0; long det_faults=0;
static long PG;
#define MAXREG 8
static uintptr_t rlo[MAXREG], rhi[MAXREG]; static int nreg=0;
#define MAXPG 65536
static uint64_t pver[MAXPG];          /* version per protected page (indexed by global page#) */
static uint64_t gclk=0;
/* the detector consults a per-fiber park_clk via this hook set by the runtime */
static __thread struct det_fib* cur_df=NULL;
void det_set_fiber(void* f){ cur_df=(struct det_fib*)f; }

static int page_index(uintptr_t a, long* outpg){
    long base=0;
    for(int i=0;i<nreg;i++){ long npg=(rhi[i]-rlo[i])/PG;
        if(a>=rlo[i] && a<rhi[i]){ *outpg = base + (a-rlo[i])/PG; return 1; }
        base+=npg; }
    return 0;
}
static void protect_regions(int prot){ for(int i=0;i<nreg;i++) mprotect((void*)rlo[i], rhi[i]-rlo[i], prot); }
void det_reprotect(void){ if(det_on) protect_regions(PROT_READ); }
void det_park(void* f){ struct det_fib* d=(struct det_fib*)f; if(det_on){ d->park_clk=gclk; protect_regions(PROT_READ); } }

static void handler(int s, siginfo_t* si, void* u){
    (void)s;(void)u; uintptr_t a=(uintptr_t)si->si_addr; long gp;
    if(!page_index(a,&gp)){ /* not ours: restore default and re-raise (real fault) */
        signal(SIGSEGV,SIG_DFL); return; }
    if(gp<MAXPG){
        if(cur_df && cur_df->active && pver[gp] > cur_df->park_clk) det_conflicts++;  /* stale RMW across park */
        gclk++; pver[gp]=gclk;
    }
    det_faults++; mprotect((void*)(a & ~(PG-1)), PG, PROT_READ|PROT_WRITE);
}

/* Protect an explicit caller-provided region (e.g. a server's shared-state arena
 * in heap/mmap), rather than the exe's data segments. Same write-fault + version
 * machinery; lets the detector guard a real server's shared state. */
void det_init_region(void* base, unsigned long len){
    PG=sysconf(_SC_PAGESIZE);
    rlo[0]=(uintptr_t)base; rhi[0]=(uintptr_t)base+len; nreg=1;
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=handler; sa.sa_flags=SA_SIGINFO; sigaction(SIGSEGV,&sa,0);
    det_on=1;
    protect_regions(PROT_READ);
    fprintf(stderr,"[detector] region %p-%p (%lu pages)\n",base,(char*)base+len,len/PG);
}

void det_init(void){
    if(!getenv("RT_DETECT")) return;
    PG=sysconf(_SC_PAGESIZE);
    char exe[4096]; int n=readlink("/proc/self/exe",exe,sizeof exe-1); if(n<=0) return; exe[n]=0;
    FILE* m=fopen("/proc/self/maps","r"); if(!m) return; char line[1024];
    while(fgets(line,sizeof line,m)){
        uintptr_t lo,hi; char perms[8]={0}; char path[1024]={0};
        /* format: lo-hi perms off dev ino path */
        if(sscanf(line,"%lx-%lx %7s %*x %*x:%*x %*u %1023[^\n]",&lo,&hi,perms,path)>=3){
            char* p=path; while(*p==' ')p++;
            int isexe = (strstr(p,exe)!=NULL);
            int isbss = (p[0]==0);   /* anonymous rw segment right after exe data = .bss */
            if(perms[0]=='r'&&perms[1]=='w'&&(isexe || isbss) && nreg<MAXREG){
                /* only take the exe's data + the adjacent anonymous bss (skip libs/heap/stack) */
                if(isexe || (nreg>0 && lo==rhi[nreg-1])){ rlo[nreg]=lo; rhi[nreg]=hi; nreg++; }
            }
        }
    }
    fclose(m);
    if(nreg==0){ return; }
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=handler; sa.sa_flags=SA_SIGINFO; sigaction(SIGSEGV,&sa,0);
    det_on=1;
    for(int i=0;i<nreg;i++) fprintf(stderr,"[detector] region %d: %lx-%lx (%lu pages)\n",i,rlo[i],rhi[i],(rhi[i]-rlo[i])/PG);
    fprintf(stderr,"[detector] protecting %d region(s)\n",nreg);
}

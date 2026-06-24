#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <ucontext.h>
#include <time.h>
#include "fw_fiber.h"
static inline uint64_t now_ns(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000ull+t.tv_nsec; }
/* ucontext ping-pong */
static ucontext_t uc_main, uc_co; static volatile long uc_n; static int UC_DONE;
static void uc_body(){ for(;;){ uc_n++; swapcontext(&uc_co,&uc_main); } }
/* fw_fiber ping-pong */
static fw_fiber f_main, f_co; static volatile long f_n;
static void f_body(void*a){ (void)a; for(;;){ f_n++; fw_fiber_switch(&f_co,&f_main); } }
int main(){
    int N=2000000;
    /* ucontext */
    static char st[256*1024]; getcontext(&uc_co); uc_co.uc_stack.ss_sp=st; uc_co.uc_stack.ss_size=sizeof st; uc_co.uc_link=&uc_main; makecontext(&uc_co,uc_body,0);
    uint64_t t0=now_ns(); for(int i=0;i<N;i++) swapcontext(&uc_main,&uc_co); uint64_t t1=now_ns();
    double uc=(double)(t1-t0)/N;
    /* fw_fiber */
    fw_fiber_init_self(&f_main); fw_fiber_make(&f_co,256*1024,f_body,0,&f_main);
    t0=now_ns(); for(int i=0;i<N;i++) fw_fiber_switch(&f_main,&f_co); t1=now_ns();
    double ff=(double)(t1-t0)/N;
    printf("ucontext swapcontext: %.1f ns/switch (round-trip %.1f ns)\n", uc, uc*2);
    printf("fw_fiber (FastWake)  : %.1f ns/switch (round-trip %.1f ns)\n", ff, ff*2);
    printf("speedup: %.1fx\n", uc/ff);
    return 0;
}

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static inline uint64_t now_ns(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000ull+t.tv_nsec; }
int main(){
  // cost of one clock_gettime
  uint64_t s=now_ns(); int N=2000000; volatile uint64_t x=0;
  for(int i=0;i<N;i++) x+=now_ns();
  uint64_t e=now_ns();
  printf("clock_gettime cost: %.1f ns/call\n", (double)(e-s)/N);
  // accuracy of a spin to 1000ns
  uint64_t a=now_ns(); while(now_ns()-a<1000){} uint64_t b=now_ns();
  printf("spin_ns(1000) actual: %lu ns\n", b-a);
  return 0;
}

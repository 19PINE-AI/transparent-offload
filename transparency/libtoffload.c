#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
static void (*real_accel)(unsigned char*,int);
static long n_intercepted=0;
/* interpose the app's offload call WITHOUT recompiling the app */
void accel_encrypt(unsigned char* buf, int n){
    if(!real_accel) real_accel = (void(*)(unsigned char*,int))dlsym(RTLD_NEXT,"accel_encrypt");
    n_intercepted++;
    /* HERE a real runtime would: submit async to the accelerator + park the coroutine.
       We call the real routine so correctness is identical; the point is the offload
       boundary was transparently captured. */
    real_accel(buf,n);
}
__attribute__((destructor)) static void report(void){
    fprintf(stderr,"[toffload shim] transparently intercepted %ld offload calls\n", n_intercepted);
}

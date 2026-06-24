/* Models a higher-latency crypto accelerator (HSM / QAT / post-quantum KEM class) at stunnel's
 * TLS record boundary. Real AES-NI crypto is preserved (correctness); we inject the accelerator's
 * per-record latency via accel_encrypt on a scratch buffer (does NOT touch the real data). Under
 * libtransparent the call yields the connection fiber (overlap); natively it blocks the thread. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
static int (*real_SSL_read)(void*,void*,int);
static int (*real_SSL_write)(void*,const void*,int);
static void (*accel)(unsigned char*,int);
__attribute__((constructor)) static void init(void){
    real_SSL_read=dlsym(RTLD_NEXT,"SSL_read");
    real_SSL_write=dlsym(RTLD_NEXT,"SSL_write");
    accel=dlsym(RTLD_DEFAULT,"accel_encrypt");
    fprintf(stderr,"[tlsoffload] loaded (accel=%p)\n",(void*)accel);
}
int SSL_read(void*s,void*buf,int n){
    int r=real_SSL_read(s,buf,n);
    if(r>0 && accel){ unsigned char d[16]={0}; accel(d,16); }   /* model decrypt offload latency */
    return r;
}
int SSL_write(void*s,const void*buf,int n){
    if(n>0 && accel){ unsigned char d[16]={0}; accel(d,16); }    /* model encrypt offload latency */
    return real_SSL_write(s,buf,n);
}

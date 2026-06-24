#define _GNU_SOURCE
#include <pthread.h>
#include <dlfcn.h>
#include <time.h>
#include <stdio.h>
static int (*rs)(pthread_cond_t*),(*rb)(pthread_cond_t*);
static int (*rw)(pthread_cond_t*,pthread_mutex_t*);
static int (*rtw)(pthread_cond_t*,pthread_mutex_t*,const struct timespec*);
__attribute__((constructor)) static void init(void){ fprintf(stderr,"[cond_versioned] loaded\n");
  rs=dlvsym(RTLD_NEXT,"pthread_cond_signal","GLIBC_2.3.2"); rb=dlvsym(RTLD_NEXT,"pthread_cond_broadcast","GLIBC_2.3.2");
  rw=dlvsym(RTLD_NEXT,"pthread_cond_wait","GLIBC_2.3.2"); rtw=dlvsym(RTLD_NEXT,"pthread_cond_timedwait","GLIBC_2.3.2"); }
int pthread_cond_signal(pthread_cond_t*c){ return rs(c); }
int pthread_cond_broadcast(pthread_cond_t*c){ return rb(c); }
int pthread_cond_wait(pthread_cond_t*c,pthread_mutex_t*m){ return rw(c,m); }
int pthread_cond_timedwait(pthread_cond_t*c,pthread_mutex_t*m,const struct timespec*t){ return rtw(c,m,t); }

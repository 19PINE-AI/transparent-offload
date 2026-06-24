/* A shared "accelerator" library used by BOTH the stock baseline and the runtime.
 * The device is a worker thread on its own core doing AES-128-CTR + a configurable
 * latency, modelling an offload accelerator. Two APIs over the same device:
 *   accel_encrypt(buf,n)         - synchronous (stock apps call this; blocks the caller)
 *   accel_submit/done/result     - async (the transparent runtime uses these to yield)
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <zlib.h>

static uint64_t now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000ull+t.tv_nsec; }
static uint64_t ACCEL_LAT_NS = 20000;     /* device latency knob (env ACCEL_LAT_US) */
static int ACCEL_OP=0; /* 0=aes 1=zip */
static unsigned char KEY[16]={1,8,15,22,29,36,43,50,57,64,71,78,85,92,99,106};

#define NSLOT 8192
struct slot { unsigned char *buf; int n; volatile uint64_t deadline; volatile int busy, done; };
static struct slot slots[NSLOT];
static volatile long sub_head=0;          /* submitted up to here (monotonic id) */
static volatile int dev_started=0;
static pthread_mutex_t startmx=PTHREAD_MUTEX_INITIALIZER;
static int DEV_CORE=6;

static void zip_roundtrip(unsigned char*b,int n){
    /* real compression work: deflate then inflate, leaving b unchanged (verifiable) */
    unsigned long clen=compressBound(n); unsigned char*c=malloc(clen);
    compress2(c,&clen,b,n,6);
    unsigned long dlen=n; unsigned char*d=malloc(n); uncompress(d,&dlen,c,clen);
    memcpy(b,d,n); free(c); free(d);
}
static void aes_ctr(unsigned char*b,int n){
    EVP_CIPHER_CTX*x=EVP_CIPHER_CTX_new(); unsigned char iv[16]={0};
    EVP_EncryptInit_ex(x,EVP_aes_128_ctr(),0,KEY,iv);
    unsigned char*tmp=malloc(n); memcpy(tmp,b,n); int ol; EVP_EncryptUpdate(x,b,&ol,tmp,n);
    free(tmp); EVP_CIPHER_CTX_free(x);
}
static void* device(void*a){ (void)a;
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(DEV_CORE,&s); sched_setaffinity(0,sizeof s,&s);
    struct sched_param p; p.sched_priority=90; sched_setscheduler(0,SCHED_FIFO,&p);
    for(;;){
        uint64_t t=now_ns();
        for(int i=0;i<NSLOT;i++){
            if(slots[i].busy && !slots[i].done && t>=slots[i].deadline){
                if(ACCEL_OP) zip_roundtrip(slots[i].buf, slots[i].n); else aes_ctr(slots[i].buf, slots[i].n);
                __sync_synchronize(); slots[i].done=1;
            }
        }
    }
    return 0;
}
static void ensure_dev(void){
    if(dev_started) return;
    pthread_mutex_lock(&startmx);
    if(!dev_started){
        char*e=getenv("ACCEL_LAT_US"); if(e) ACCEL_LAT_NS=(uint64_t)atoll(e)*1000;
        char*c=getenv("ACCEL_CORE"); if(c) DEV_CORE=atoi(c);
        char*o=getenv("ACCEL_OP"); if(o&&!strcmp(o,"zip")) ACCEL_OP=1;
        pthread_t th; pthread_create(&th,0,device,0); pthread_detach(th);
        dev_started=1;
    }
    pthread_mutex_unlock(&startmx);
}

/* async API */
long accel_submit(unsigned char*buf,int n){
    ensure_dev();
    long id=__sync_fetch_and_add(&sub_head,1); int i=id%NSLOT;
    slots[i].buf=buf; slots[i].n=n; slots[i].done=0; slots[i].deadline=now_ns()+ACCEL_LAT_NS;
    __sync_synchronize(); slots[i].busy=1;
    return id;
}
int accel_done(long id){ return slots[id%NSLOT].done; }
void accel_release(long id){ slots[id%NSLOT].busy=0; }

/* synchronous API (stock apps): submit + busy-block until done */
void accel_encrypt(unsigned char*buf,int n){
    long id=accel_submit(buf,n);
    while(!accel_done(id)){ struct timespec ts={0,1000}; nanosleep(&ts,0); }  /* block the caller */
    accel_release(id);
}

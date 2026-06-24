/* libaccel_gpu.so — same accel API, but the accelerator is the REAL GPU (AES-CTR).
 * async: accel_submit launches a GPU kernel on a pooled stream + records an event;
 *        accel_done = cudaEventQuery. Parallel device (many streams) -> not single-core bound. */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <cuda_runtime.h>
#include "../gpu/aes_dev.cuh"
static unsigned char KEY[16]={1,8,15,22,29,36,43,50,57,64,71,78,85,92,99,106};
#define NSLOT 4096
struct gslot { cudaStream_t st; cudaEvent_t ev; unsigned char*h; unsigned char*d; unsigned char*ubuf; int n; int inited; };
static gslot slots[NSLOT];
static volatile long sub_head=0;
static int started=0; static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER;
static void ensure(void){ if(started) return; pthread_mutex_lock(&mx); if(!started){
    uint8_t rk[176]; key_expand(KEY,rk); cudaMemcpyToSymbol(d_sbox,SBOX,256); cudaMemcpyToSymbol(d_rk,rk,176);
    started=1; } pthread_mutex_unlock(&mx); }
extern "C" long accel_submit(unsigned char*buf,int n){
    ensure(); long id=__sync_fetch_and_add(&sub_head,1); int i=id%NSLOT;
    if(!slots[i].inited){ cudaStreamCreate(&slots[i].st); cudaEventCreate(&slots[i].ev); cudaMallocHost(&slots[i].h,8192); cudaMalloc(&slots[i].d,8192); slots[i].inited=1; }
    memcpy(slots[i].h,buf,n); slots[i].ubuf=buf; slots[i].n=n;
    uint64_t nb=n/16;
    cudaMemcpyAsync(slots[i].d,slots[i].h,n,cudaMemcpyHostToDevice,slots[i].st);
    aes_ctr<<<(unsigned)((nb+255)/256),256,0,slots[i].st>>>(slots[i].d,nb,0);
    cudaMemcpyAsync(slots[i].h,slots[i].d,n,cudaMemcpyDeviceToHost,slots[i].st);
    cudaEventRecord(slots[i].ev,slots[i].st);
    return id;
}
extern "C" int accel_done(long id){ int i=id%NSLOT; if(cudaEventQuery(slots[i].ev)==cudaSuccess){ memcpy(slots[i].ubuf,slots[i].h,slots[i].n); return 1; } return 0; }
extern "C" void accel_release(long id){ (void)id; }
extern "C" void accel_encrypt(unsigned char*buf,int n){ int i=accel_submit(buf,n)%NSLOT; cudaStreamSynchronize(slots[i].st); memcpy(buf,slots[i].h,n); }

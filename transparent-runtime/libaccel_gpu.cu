/* libaccel_gpu.so — same accel API, but the accelerator is the REAL GPU (AES-CTR).
 * async: accel_submit launches a GPU kernel on a pooled stream + records an event;
 *        accel_done = cudaEventQuery. Parallel device (many streams) -> not single-core bound.
 *
 * The AES block size is the offload weight: env ACCEL_AES_BYTES (default 1 MiB) models
 * realistic BULK crypto (e.g. a proxy terminating TLS on long, high-throughput
 * connections), which is bandwidth/compute-bound real GPU work -- not the tiny,
 * launch-bound 4 KB case. Set it small (4096) for the launch-bound regime. */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <cuda_runtime.h>
#include "../gpu/aes_dev.cuh"
static unsigned char KEY[16]={1,8,15,22,29,36,43,50,57,64,71,78,85,92,99,106};
#define NSLOT 256                      /* cap in-flight; mem ~ NSLOT * 2 * SZ */
static int SZ = 1048576;               /* AES bytes per offload (env ACCEL_AES_BYTES) */
struct gslot { cudaStream_t st; cudaEvent_t ev; unsigned char*h; unsigned char*d; unsigned char*ubuf; int n; int inited; };
static gslot slots[NSLOT];
static volatile long sub_head=0;
static int started=0; static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER;
static void ensure(void){ if(started) return; pthread_mutex_lock(&mx); if(!started){
    char*e=getenv("ACCEL_AES_BYTES"); if(e){ SZ=atoi(e); if(SZ<16) SZ=16; SZ-=SZ%16; }
    uint8_t rk[176]; key_expand(KEY,rk); cudaMemcpyToSymbol(d_sbox,SBOX,256); cudaMemcpyToSymbol(d_rk,rk,176);
    started=1; } pthread_mutex_unlock(&mx); }
extern "C" long accel_submit(unsigned char*buf,int n){
    ensure(); long id=__sync_fetch_and_add(&sub_head,1); int i=id%NSLOT;
    if(!slots[i].inited){ cudaStreamCreate(&slots[i].st); cudaEventCreate(&slots[i].ev); cudaMallocHost(&slots[i].h,SZ); cudaMalloc(&slots[i].d,SZ); slots[i].inited=1; }
    int cp = n<SZ?n:SZ; memcpy(slots[i].h,buf,cp); slots[i].ubuf=buf; slots[i].n=n;
    uint64_t nb=SZ/16;                                  /* encrypt the full SZ-byte block */
    cudaMemcpyAsync(slots[i].d,slots[i].h,SZ,cudaMemcpyHostToDevice,slots[i].st);
    aes_ctr<<<(unsigned)((nb+255)/256),256,0,slots[i].st>>>(slots[i].d,nb,0);
    cudaMemcpyAsync(slots[i].h,slots[i].d,SZ,cudaMemcpyDeviceToHost,slots[i].st);
    cudaEventRecord(slots[i].ev,slots[i].st);
    return id;
}
extern "C" int accel_done(long id){ int i=id%NSLOT; if(cudaEventQuery(slots[i].ev)==cudaSuccess){ int cp=slots[i].n<SZ?slots[i].n:SZ; memcpy(slots[i].ubuf,slots[i].h,cp); return 1; } return 0; }
extern "C" void accel_release(long id){ (void)id; }
extern "C" void accel_encrypt(unsigned char*buf,int n){ int i=accel_submit(buf,n)%NSLOT; cudaStreamSynchronize(slots[i].st); int cp=n<SZ?n:SZ; memcpy(buf,slots[i].h,cp); }

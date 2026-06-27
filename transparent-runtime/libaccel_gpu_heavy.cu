/* libaccel_gpu_heavy.so — same accel_* API as libaccel_gpu.so, but the offload is a
 * REAL compute-heavy GPU kernel (cuBLAS SGEMM, the core of DNN inference), not an
 * emulated software latency. The real GPU latency is tuned by problem size so we can
 * reach the ms regime honestly with measured GPU time:
 *
 *   ACCEL_GEMM_N      square GEMM dimension N (latency ~ N^3)   [default 1024]
 *   ACCEL_GEMM_ITERS  repeat the GEMM K times per offload       [default 1]
 *
 * async: accel_submit launches the GEMM on a pooled stream + records an event;
 *        accel_done = cudaEventQuery (parallel device, many streams overlap).
 * sync:  accel_encrypt = submit + cudaStreamSynchronize.
 *
 * Drop-in for the existing integrations (redis/nginx/node/py/pg/maria/haproxy):
 * they call accel_encrypt / accel_submit and now get real GPU compute latency.
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <cuda_runtime.h>
#include <cublas_v2.h>

#define NSLOT 256                 /* cap on concurrent in-flight offloads (memory ~ NSLOT*3*N^2*4) */
static int   GEMM_N    = 1024;
static int   GEMM_ITERS = 1;
static float ALPHA = 1.0f, BETA = 0.0f;

struct gslot {
    cudaStream_t  st;
    cudaEvent_t   ev;
    cublasHandle_t h;
    float        *dA, *dB, *dC;   /* N*N device operands, allocated once, reused */
    int           inited;
};
static gslot slots[NSLOT];
static volatile long sub_head = 0;
static int started = 0;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;

static void ensure(void){
    if (started) return;
    pthread_mutex_lock(&mx);
    if (!started){
        char *n = getenv("ACCEL_GEMM_N");     if (n) GEMM_N     = atoi(n);
        char *k = getenv("ACCEL_GEMM_ITERS"); if (k) GEMM_ITERS = atoi(k);
        if (GEMM_N    < 16) GEMM_N = 16;
        if (GEMM_ITERS < 1) GEMM_ITERS = 1;
        started = 1;
    }
    pthread_mutex_unlock(&mx);
}

static void init_slot(int i){
    cudaStreamCreate(&slots[i].st);
    cudaEventCreateWithFlags(&slots[i].ev, cudaEventDisableTiming);
    cublasCreate(&slots[i].h);
    cublasSetStream(slots[i].h, slots[i].st);
    size_t bytes = (size_t)GEMM_N * GEMM_N * sizeof(float);
    cudaMalloc(&slots[i].dA, bytes);
    cudaMalloc(&slots[i].dB, bytes);
    cudaMalloc(&slots[i].dC, bytes);
    cudaMemsetAsync(slots[i].dA, 1, bytes, slots[i].st);
    cudaMemsetAsync(slots[i].dB, 1, bytes, slots[i].st);
    slots[i].inited = 1;
}

/* launch K back-to-back GEMMs on the slot's stream; record completion event */
static void launch(int i){
    int N = GEMM_N;
    for (int k = 0; k < GEMM_ITERS; k++)
        cublasSgemm(slots[i].h, CUBLAS_OP_N, CUBLAS_OP_N, N, N, N,
                    &ALPHA, slots[i].dA, N, slots[i].dB, N, &BETA, slots[i].dC, N);
    cudaEventRecord(slots[i].ev, slots[i].st);
}

extern "C" long accel_submit(unsigned char *buf, int n){
    (void)buf; (void)n;                 /* compute-latency model: operands are fixed on-device */
    ensure();
    long id = __sync_fetch_and_add(&sub_head, 1);
    int  i  = id % NSLOT;
    if (!slots[i].inited) init_slot(i);
    launch(i);
    return id;
}

extern "C" int  accel_done(long id){
    return cudaEventQuery(slots[id % NSLOT].ev) == cudaSuccess ? 1 : 0;
}
extern "C" void accel_release(long id){ (void)id; }

extern "C" void accel_encrypt(unsigned char *buf, int n){
    (void)buf; (void)n;
    ensure();
    long id = __sync_fetch_and_add(&sub_head, 1);
    int  i  = id % NSLOT;
    if (!slots[i].inited) init_slot(i);
    launch(i);
    cudaStreamSynchronize(slots[i].st);
}

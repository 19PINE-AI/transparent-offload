/* gemm_calib — measure the REAL GPU latency of the heavy offload (cuBLAS SGEMM)
 * across a range of sizes, so the weight sweep's x-axis is real measured GPU time.
 * Prints CSV: gemm_n,iters,gpu_lat_us  (single-stream, uncontended latency).
 * Usage: ./gemm_calib [iters]            (default iters=1)
 */
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cuda_runtime.h>
#include <cublas_v2.h>

static double time_gemm(cublasHandle_t h, cudaStream_t st, int N, int iters,
                        float *dA, float *dB, float *dC){
    const float a = 1.f, b = 0.f;
    // warmup
    for (int k=0;k<3;k++)
        cublasSgemm(h,CUBLAS_OP_N,CUBLAS_OP_N,N,N,N,&a,dA,N,dB,N,&b,dC,N);
    cudaStreamSynchronize(st);
    cudaEvent_t e0,e1; cudaEventCreate(&e0); cudaEventCreate(&e1);
    int reps = 20;
    cudaEventRecord(e0,st);
    for (int r=0;r<reps;r++)
        for (int k=0;k<iters;k++)
            cublasSgemm(h,CUBLAS_OP_N,CUBLAS_OP_N,N,N,N,&a,dA,N,dB,N,&b,dC,N);
    cudaEventRecord(e1,st); cudaEventSynchronize(e1);
    float ms=0; cudaEventElapsedTime(&ms,e0,e1);
    cudaEventDestroy(e0); cudaEventDestroy(e1);
    return (double)ms*1000.0/reps;   // us per offload (iters GEMMs)
}

int main(int argc,char**argv){
    int iters = argc>1 ? atoi(argv[1]) : 1;
    int Ns[] = {128,256,512,768,1024,1536,2048,3072,4096};
    cublasHandle_t h; cublasCreate(&h);
    cudaStream_t st; cudaStreamCreate(&st); cublasSetStream(h,st);
    printf("gemm_n,iters,gpu_lat_us\n");
    for (int N : Ns){
        size_t bytes=(size_t)N*N*sizeof(float);
        float *dA,*dB,*dC;
        if (cudaMalloc(&dA,bytes)||cudaMalloc(&dB,bytes)||cudaMalloc(&dC,bytes)){
            fprintf(stderr,"OOM at N=%d (need %.0f MiB x3)\n",N,bytes/1048576.0); break;
        }
        cudaMemset(dA,1,bytes); cudaMemset(dB,1,bytes);
        double us=time_gemm(h,st,N,iters,dA,dB,dC);
        printf("%d,%d,%.2f\n",N,iters,us); fflush(stdout);
        cudaFree(dA); cudaFree(dB); cudaFree(dC);
    }
    return 0;
}

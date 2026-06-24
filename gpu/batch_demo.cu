// Phase 3.3b: what automatic batch formation buys on GPU crypto.
// A transparent-coroutine runtime sees ALL parked offloads at the dispatcher, so it can
// fuse them into one kernel launch (amortizing the ~11µs launch+PCIe floor) — getting
// SSLShader-style batching with no application rewrite. Here we measure the upside:
// B independent AES requests done as B separate kernels (per-request) vs one fused kernel.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cuda_runtime.h>
#include "aes_dev.cuh"
#define CK(x) do{ cudaError_t e=(x); if(e!=cudaSuccess){ fprintf(stderr,"CUDA %s\n",cudaGetErrorString(e)); return 1;} }while(0)
static double now_ms(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec/1e6; }

int main(){
    uint8_t key[16]; for(int i=0;i<16;i++)key[i]=i*7+1; uint8_t rk[176]; key_expand(key,rk);
    CK(cudaMemcpyToSymbol(d_sbox,SBOX,256)); CK(cudaMemcpyToSymbol(d_rk,rk,176));
    int B=64, REP=300;
    cudaStream_t streams[256]; for(int i=0;i<B;i++) cudaStreamCreate(&streams[i]);
    cudaStream_t one; cudaStreamCreate(&one);
    printf("# B=%d requests; per-request = B kernels, batched = 1 fused kernel\n",B);
    printf("# msg_bytes,perreq_ops_s,batched_ops_s,batch_speedup\n");
    for(size_t MSG : {256ul,1024ul,4096ul,16384ul,65536ul}){
        size_t tot=(size_t)B*MSG; uint64_t nbtot=tot/16, nbmsg=MSG/16;
        uint8_t *h,*d; CK(cudaMallocHost(&h,tot)); CK(cudaMalloc(&d,tot)); memset(h,0xAB,tot);
        // per-request: B independent memcpy+kernel+memcpy on B streams
        std::vector<double> pr,ba;
        for(int r=0;r<REP;r++){
            double t0=now_ms();
            for(int i=0;i<B;i++){
                uint8_t*hi=h+i*MSG,*di=d+i*MSG;
                cudaMemcpyAsync(di,hi,MSG,cudaMemcpyHostToDevice,streams[i]);
                aes_ctr<<<(unsigned)((nbmsg+255)/256),256,0,streams[i]>>>(di,nbmsg,0);
                cudaMemcpyAsync(hi,di,MSG,cudaMemcpyDeviceToHost,streams[i]);
            }
            cudaDeviceSynchronize();
            pr.push_back(now_ms()-t0);
        }
        for(int r=0;r<REP;r++){
            double t0=now_ms();
            cudaMemcpyAsync(d,h,tot,cudaMemcpyHostToDevice,one);
            aes_ctr<<<(unsigned)((nbtot+255)/256),256,0,one>>>(d,nbtot,0);   // one fused kernel
            cudaMemcpyAsync(h,d,tot,cudaMemcpyDeviceToHost,one);
            cudaStreamSynchronize(one);
            ba.push_back(now_ms()-t0);
        }
        std::sort(pr.begin(),pr.end()); std::sort(ba.begin(),ba.end());
        double prm=pr[REP/2], bam=ba[REP/2];           // median ms for B requests
        double pr_ops=B/(prm/1e3), ba_ops=B/(bam/1e3);
        printf("%zu,%.0f,%.0f,%.2f\n", MSG, pr_ops, ba_ops, ba_ops/pr_ops);
        cudaFreeHost(h); cudaFree(d);
    }
    return 0;
}

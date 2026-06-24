// GPU AES-128-CTR crypto accelerator + offload latency/throughput microbench (Phase 3.0).
// Goal: pin the offload-latency regime (launch+PCIe floor, latency vs batch) and
// verify correctness against OpenSSL. Each CUDA thread encrypts one 16-byte block.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cuda_runtime.h>
#include <openssl/evp.h>

#define CK(x) do{ cudaError_t e=(x); if(e!=cudaSuccess){ \
  fprintf(stderr,"CUDA %s:%d %s\n",__FILE__,__LINE__,cudaGetErrorString(e)); exit(1);} }while(0)

__constant__ uint8_t d_sbox[256];
__constant__ uint8_t d_rk[176];   // 11 round keys

static const uint8_t SBOX[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

static void key_expand(const uint8_t key[16], uint8_t rk[176]){
    static const uint8_t rcon[10]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
    memcpy(rk,key,16);
    for(int i=4;i<44;i++){
        uint8_t t[4]; memcpy(t, rk+(i-1)*4, 4);
        if(i%4==0){
            uint8_t tmp=t[0]; t[0]=SBOX[t[1]]^rcon[i/4-1]; t[1]=SBOX[t[2]]; t[2]=SBOX[t[3]]; t[3]=SBOX[tmp];
        }
        for(int j=0;j<4;j++) rk[i*4+j]=rk[(i-4)*4+j]^t[j];
    }
}

__device__ __forceinline__ uint8_t xtime(uint8_t x){ return (uint8_t)((x<<1) ^ ((x>>7)*0x1b)); }

// AES-128 encrypt one 16-byte block in place (state row-major as 16 bytes column order)
__device__ void aes_encrypt_block(uint8_t s[16]){
    #pragma unroll
    for(int i=0;i<16;i++) s[i]^=d_rk[i];
    for(int round=1; round<=10; round++){
        // SubBytes
        #pragma unroll
        for(int i=0;i<16;i++) s[i]=d_sbox[s[i]];
        // ShiftRows (state columns are s[0..3]=col0 ...; rows are s[r], s[r+4], s[r+8], s[r+12])
        uint8_t t;
        t=s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;            // row1 <<1
        t=s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;     // row2 <<2
        t=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;          // row3 <<3 (i.e. >>1)
        if(round!=10){
            #pragma unroll
            for(int c=0;c<4;c++){
                uint8_t *p=s+c*4;
                uint8_t a0=p[0],a1=p[1],a2=p[2],a3=p[3];
                uint8_t h=a0^a1^a2^a3;
                p[0]^=h^xtime(a0^a1);
                p[1]^=h^xtime(a1^a2);
                p[2]^=h^xtime(a2^a3);
                p[3]^=h^xtime(a3^a0);
            }
        }
        #pragma unroll
        for(int i=0;i<16;i++) s[i]^=d_rk[round*16+i];
    }
}

// CTR keystream: block counter = base + idx, big-endian in last 8 bytes; XOR into data.
__global__ void aes_ctr_kernel(uint8_t* data, uint64_t nblocks, uint64_t base){
    uint64_t idx = blockIdx.x*(uint64_t)blockDim.x + threadIdx.x;
    if(idx>=nblocks) return;
    uint8_t ctr[16];
    #pragma unroll
    for(int i=0;i<8;i++) ctr[i]=0;
    uint64_t c = base+idx;
    #pragma unroll
    for(int i=0;i<8;i++) ctr[15-i]=(uint8_t)(c>>(8*i));
    aes_encrypt_block(ctr);
    uint8_t* d=data+idx*16;
    #pragma unroll
    for(int i=0;i<16;i++) d[i]^=ctr[i];
}

static double now_ms(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3+t.tv_nsec/1e6; }

int main(int argc,char**argv){
    uint8_t key[16]; for(int i=0;i<16;i++) key[i]=i*7+1;
    uint8_t rk[176]; key_expand(key,rk);
    CK(cudaMemcpyToSymbol(d_sbox,SBOX,256));
    CK(cudaMemcpyToSymbol(d_rk,rk,176));

    // ---- correctness vs OpenSSL AES-128-ECB on the keystream (CTR with zero plaintext) ----
    {
        const uint64_t N=4; uint8_t *buf; CK(cudaMallocManaged(&buf,N*16));
        memset(buf,0,N*16);
        aes_ctr_kernel<<<1,N>>>(buf,N,0); CK(cudaDeviceSynchronize());
        EVP_CIPHER_CTX*c=EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(c,EVP_aes_128_ecb(),0,key,0); EVP_CIPHER_CTX_set_padding(c,0);
        int ok=1;
        for(uint64_t b=0;b<N;b++){
            uint8_t in[16]={0}; in[15]=(uint8_t)b; uint8_t out[16]; int ol=0;
            EVP_EncryptUpdate(c,out,&ol,in,16);
            if(memcmp(out,buf+b*16,16)!=0) ok=0;
        }
        EVP_CIPHER_CTX_free(c);
        printf("# correctness vs OpenSSL AES-128: %s\n", ok?"PASS":"FAIL");
        if(!ok){ printf("  (GPU keystream != OpenSSL) aborting\n"); return 1; }
        CK(cudaFree(buf));
    }

    // ---- latency / throughput vs batch size ----
    printf("# bytes,blocks,e2e_us_median,kern_us_median,e2e_GBps,kern_GBps\n");
    size_t sizes[]={64,256,1024,4096,16384,65536,262144,1048576,4194304,16777216,67108864};
    int TPB=256, REP=200;
    cudaStream_t st; CK(cudaStreamCreate(&st));
    cudaEvent_t k0,k1; CK(cudaEventCreate(&k0)); CK(cudaEventCreate(&k1));
    for(size_t S: sizes){
        uint64_t nb = (S+15)/16;
        uint8_t *h; CK(cudaMallocHost(&h,S));            // pinned
        uint8_t *d; CK(cudaMalloc(&d,S));
        memset(h,0xAB,S);
        std::vector<double> e2e, kern;
        for(int r=0;r<REP;r++){
            double t0=now_ms();
            CK(cudaMemcpyAsync(d,h,S,cudaMemcpyHostToDevice,st));
            CK(cudaEventRecord(k0,st));
            aes_ctr_kernel<<<(unsigned)((nb+TPB-1)/TPB),TPB,0,st>>>(d,nb,0);
            CK(cudaEventRecord(k1,st));
            CK(cudaMemcpyAsync(h,d,S,cudaMemcpyDeviceToHost,st));
            CK(cudaStreamSynchronize(st));
            double t1=now_ms();
            float kms=0; CK(cudaEventElapsedTime(&kms,k0,k1));
            e2e.push_back((t1-t0)*1000.0); kern.push_back(kms*1000.0);
        }
        std::sort(e2e.begin(),e2e.end()); std::sort(kern.begin(),kern.end());
        double em=e2e[REP/2], km=kern[REP/2];
        printf("%zu,%lu,%.2f,%.2f,%.2f,%.2f\n", S, nb, em, km,
               S/1e9/(em/1e6), S/1e9/(km/1e6));
        CK(cudaFreeHost(h)); CK(cudaFree(d));
    }
    return 0;
}

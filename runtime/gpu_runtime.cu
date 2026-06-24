// Phase 1 demonstration: transparent-offload runtime on a REAL GPU.
// N "connections" = FastWake fibers on ONE CPU thread. Each offloads AES-CTR to
// the GPU on its own CUDA stream, records a cudaEvent, and PARKS. The dispatcher
// polls cudaEventQuery (the FastWake dispatcher loop with CUDA events in place of
// RDMA CQs) and resumes whichever fiber's GPU op finished. Compared against
// "busy": one connection, synchronous cudaStreamSynchronize per op (CPU stalls).
//
// Result of interest: the mechanism works and AES is correct vs OpenSSL; under an
// UNCONTENDED GPU, coro overlaps GPU latency with other connections' CPU work.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <cuda_runtime.h>
#include <pthread.h>
#include <unistd.h>
#include <openssl/evp.h>
extern "C" {
  #include "fw_fiber.h"
}
#define CK(x) do{ cudaError_t e=(x); if(e!=cudaSuccess){ \
  fprintf(stderr,"CUDA %s:%d %s\n",__FILE__,__LINE__,cudaGetErrorString(e)); exit(1);} }while(0)

// ---- AES-128 device code (same as aes_gpu.cu, verified vs OpenSSL) ----
__constant__ uint8_t d_sbox[256]; __constant__ uint8_t d_rk[176];
static const uint8_t SBOX[256]={0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
static void key_expand(const uint8_t key[16],uint8_t rk[176]){ static const uint8_t rc[10]={1,2,4,8,16,32,64,128,0x1b,0x36}; memcpy(rk,key,16); for(int i=4;i<44;i++){uint8_t t[4]; memcpy(t,rk+(i-1)*4,4); if(i%4==0){uint8_t tmp=t[0]; t[0]=SBOX[t[1]]^rc[i/4-1]; t[1]=SBOX[t[2]]; t[2]=SBOX[t[3]]; t[3]=SBOX[tmp];} for(int j=0;j<4;j++) rk[i*4+j]=rk[(i-4)*4+j]^t[j];}}
__device__ __forceinline__ uint8_t xt(uint8_t x){return (uint8_t)((x<<1)^((x>>7)*0x1b));}
__device__ void aes_enc(uint8_t s[16]){ for(int i=0;i<16;i++) s[i]^=d_rk[i]; for(int r=1;r<=10;r++){ for(int i=0;i<16;i++) s[i]=d_sbox[s[i]]; uint8_t t; t=s[1];s[1]=s[5];s[5]=s[9];s[9]=s[13];s[13]=t; t=s[2];s[2]=s[10];s[10]=t;t=s[6];s[6]=s[14];s[14]=t; t=s[15];s[15]=s[11];s[11]=s[7];s[7]=s[3];s[3]=t; if(r!=10){for(int c=0;c<4;c++){uint8_t*p=s+c*4,a0=p[0],a1=p[1],a2=p[2],a3=p[3],h=a0^a1^a2^a3; p[0]^=h^xt(a0^a1);p[1]^=h^xt(a1^a2);p[2]^=h^xt(a2^a3);p[3]^=h^xt(a3^a0);}} for(int i=0;i<16;i++) s[i]^=d_rk[r*16+i]; }}
__global__ void aes_ctr(uint8_t*data,uint64_t nb,uint64_t base){ uint64_t idx=blockIdx.x*(uint64_t)blockDim.x+threadIdx.x; if(idx>=nb)return; uint8_t ctr[16]; for(int i=0;i<8;i++)ctr[i]=0; uint64_t c=base+idx; for(int i=0;i<8;i++)ctr[15-i]=(uint8_t)(c>>(8*i)); aes_enc(ctr); uint8_t*d=data+idx*16; for(int i=0;i<16;i++) d[i]^=ctr[i]; }

static inline uint64_t now_ns(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000000ull+t.tv_nsec;}
static void spin_ns(uint64_t ns){uint64_t s=now_ns();while(now_ns()-s<ns) __asm__ __volatile__("pause");}

static int N=32, MSG=4096, RUNMS=2000; static uint64_t WCPU=2000;
static volatile int stop=0;
struct Conn { cudaStream_t st; cudaEvent_t ev; uint8_t *h,*d; int waiting; int finished; long ops; fw_fiber fb; };
static std::vector<Conn> conns; static fw_fiber sched; static int cur;

static void submit_offload(Conn&c){
    uint64_t nb=MSG/16;
    cudaMemcpyAsync(c.d,c.h,MSG,cudaMemcpyHostToDevice,c.st);
    aes_ctr<<<(unsigned)((nb+255)/256),256,0,c.st>>>(c.d,nb,0);
    cudaMemcpyAsync(c.h,c.d,MSG,cudaMemcpyDeviceToHost,c.st);
    cudaEventRecord(c.ev,c.st);
}
static void body(void*arg){
    int id=(int)(long)arg; Conn&c=conns[id];
    while(!stop){
        spin_ns(WCPU/2);                 // preprocess
        memset(c.h,(uint8_t)(id+1),MSG);
        submit_offload(c);               // launch GPU AES on this stream
        c.waiting=1; fw_fiber_switch(&c.fb,&sched);   // PARK at offload
        spin_ns(WCPU/2);                 // postprocess (use result)
        c.ops++;
    }
    c.finished=1;
}
// dispatcher: run runnable fibers; poll cudaEventQuery to wake parked ones
static long run_coro(){
    fw_fiber_init_self(&sched);
    for(int i=0;i<N;i++){ conns[i].waiting=0; conns[i].finished=0; conns[i].ops=0;
        fw_fiber_make(&conns[i].fb,64*1024,body,(void*)(long)i,&sched); }
    for(;;){
        for(int i=0;i<N;i++) if(conns[i].waiting && cudaEventQuery(conns[i].ev)==cudaSuccess) conns[i].waiting=0;
        int ran=0,parked=0,fin=0;
        for(int i=0;i<N;i++){ if(conns[i].finished){fin++;continue;} if(conns[i].waiting){parked++;continue;}
            cur=i; fw_fiber_switch(&sched,&conns[i].fb); ran++; }
        if(fin==N) break;
        if(ran==0&&parked>0) __asm__ __volatile__("pause");
    }
    long t=0; for(int i=0;i<N;i++) t+=conns[i].ops; return t;
}
// busy: 1 connection, synchronous per op (CPU stalls during GPU)
static long run_busy(){
    Conn&c=conns[0]; long ops=0;
    while(!stop){ spin_ns(WCPU/2); memset(c.h,7,MSG); submit_offload(c);
        cudaStreamSynchronize(c.st); spin_ns(WCPU/2); ops++; }
    return ops;
}

int main(int argc,char**argv){
    const char*mode=argc>1?argv[1]:"coro";
    if(argc>2) N=atoi(argv[2]); if(argc>3) MSG=atoi(argv[3]); if(argc>4) WCPU=strtoull(argv[4],0,10);
    if(!strcmp(mode,"busy")) N=1;
    uint8_t key[16]; for(int i=0;i<16;i++)key[i]=i*7+1; uint8_t rk[176]; key_expand(key,rk);
    CK(cudaMemcpyToSymbol(d_sbox,SBOX,256)); CK(cudaMemcpyToSymbol(d_rk,rk,176));
    conns.resize(N);
    for(int i=0;i<N;i++){ CK(cudaStreamCreate(&conns[i].st)); CK(cudaEventCreate(&conns[i].ev));
        CK(cudaMallocHost(&conns[i].h,MSG)); CK(cudaMalloc(&conns[i].d,MSG)); }

    // correctness: one op vs OpenSSL AES-128-CTR
    memset(conns[0].h,0,MSG); submit_offload(conns[0]); CK(cudaStreamSynchronize(conns[0].st));
    { EVP_CIPHER_CTX*x=EVP_CIPHER_CTX_new(); uint8_t iv[16]={0}; EVP_EncryptInit_ex(x,EVP_aes_128_ctr(),0,key,iv);
      std::vector<uint8_t> ref(MSG,0),out(MSG); int ol; EVP_EncryptUpdate(x,out.data(),&ol,ref.data(),MSG);
      int ok=memcmp(out.data(),conns[0].h,MSG)==0; EVP_CIPHER_CTX_free(x);
      printf("# GPU AES-CTR correctness vs OpenSSL: %s\n", ok?"PASS":"FAIL"); if(!ok) return 1; }

    // timed run
    pthread_t tmr; uint64_t t0;
    { stop=0; }
    // simple timer via thread
    struct A{static void* f(void*a){uint64_t s=now_ns(); while(now_ns()-s<(uint64_t)RUNMS*1000000ull&&!stop) usleep(2000); stop=1; return 0;}};
    pthread_create(&tmr,0,A::f,0);
    t0=now_ns();
    long ops = !strcmp(mode,"busy")? run_busy() : run_coro();
    double dt=(now_ns()-t0)/1e9;
    pthread_join(tmr,0);
    printf("%s,N=%d,MSG=%d,WCPU_us=%.1f,ops=%ld,throughput_ops_s=%.0f,GBps=%.2f\n",
        mode,N,MSG,WCPU/1000.0,ops,ops/dt, (double)ops*MSG/1e9/dt);
    return 0;
}

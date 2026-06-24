#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <time.h>
#include <openssl/evp.h>
#define REQ 4096
static int PORT=20443, NCONN=64, RUN_MS=2000;
static volatile int stop=0; static volatile long ok=0,bad=0;
static uint64_t *lat; static volatile long latn=0;
static unsigned char KEY[16]={1,8,15,22,29,36,43,50,57,64,71,78,85,92,99,106};
static uint64_t now_ns(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000ull+t.tv_nsec; }
static void expect(unsigned char*b,int n){ EVP_CIPHER_CTX*x=EVP_CIPHER_CTX_new(); unsigned char iv[16]={0};
    EVP_EncryptInit_ex(x,EVP_aes_128_ctr(),0,KEY,iv); unsigned char*t=malloc(n); memcpy(t,b,n); int ol; EVP_EncryptUpdate(x,b,&ol,t,n); free(t); EVP_CIPHER_CTX_free(x); }
static void* cli(void*a){ (void)a;
    int fd=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in sa; memset(&sa,0,sizeof sa);
    sa.sin_family=AF_INET; sa.sin_port=htons(PORT); sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(struct sockaddr*)&sa,sizeof sa)<0) return 0;
    int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    unsigned char req[REQ],exp[REQ],rsp[REQ]; for(int i=0;i<REQ;i++) req[i]=(unsigned char)(i*131+7);
    memcpy(exp,req,REQ); char*op=getenv("ACCEL_OP"); if(!(op&&!strcmp(op,"zip"))) expect(exp,REQ);
    while(!stop){ uint64_t t0=now_ns();
        int p=0; while(p<REQ){ int r=write(fd,req+p,REQ-p); if(r<=0) goto out; p+=r; }
        int g=0; while(g<REQ){ int r=read(fd,rsp+g,REQ-g); if(r<=0) goto out; g+=r; }
        if(memcmp(rsp,exp,REQ)==0) __sync_fetch_and_add(&ok,1); else __sync_fetch_and_add(&bad,1);
        long s=__sync_fetch_and_add(&latn,1); lat[s%300000]=now_ns()-t0; }
out: close(fd); return 0; }
static int cmp(const void*a,const void*b){ uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b; return x<y?-1:x>y?1:0; }
int main(int argc,char**argv){ if(argc>1)NCONN=atoi(argv[1]); if(argc>2)RUN_MS=atoi(argv[2]); if(argc>3)PORT=atoi(argv[3]);
    lat=malloc(sizeof(uint64_t)*300000);
    pthread_t th[1024]; uint64_t t0=now_ns();
    for(int i=0;i<NCONN;i++) pthread_create(&th[i],0,cli,0);
    usleep(RUN_MS*1000); stop=1; for(int i=0;i<NCONN;i++) pthread_join(th[i],0);
    double dt=(now_ns()-t0)/1e9; long n=latn<300000?latn:300000; qsort(lat,n,sizeof(uint64_t),cmp);
    printf("NCONN=%d ok=%ld bad=%ld tput_rps=%.0f p50_us=%.1f p99_us=%.1f\n",NCONN,ok,bad,(ok+bad)/dt,
        n?lat[n/2]/1000.0:0,n?lat[(long)(n*0.99)]/1000.0:0);
    return 0; }

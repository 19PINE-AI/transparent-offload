/* A DIFFERENT stock-style binary: a DNN-inference server. Thread-per-connection,
 * but with non-trivial pre/post-processing around the offload (normalize -> GPU
 * inference offload -> format), exercising a different code path than conn_server.
 * The runtime knows nothing about it. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <time.h>
#define REQ 4096
extern void accel_encrypt(unsigned char*,int);    /* the inference offload (GPU) */
static int PORT=20460;
static void spin_us(int us){ struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
    for(;;){ clock_gettime(CLOCK_MONOTONIC,&t1); if((t1.tv_sec-t0.tv_sec)*1000000+(t1.tv_nsec-t0.tv_nsec)/1000>=us) break; } }
static void* conn(void*a){ int fd=(int)(long)a; int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    unsigned char buf[REQ];
    for(;;){
        int g=0; while(g<REQ){ int r=read(fd,buf+g,REQ-g); if(r<=0) goto done; g+=r; }
        spin_us(3);              /* preprocess (normalize) */
        accel_encrypt(buf,REQ);  /* the GPU inference offload */
        spin_us(3);              /* postprocess (format) */
        int p=0; while(p<REQ){ int r=write(fd,buf+p,REQ-p); if(r<=0) goto done; p+=r; }
    }
done: close(fd); return 0; }
int main(int argc,char**argv){ if(argc>1)PORT=atoi(argv[1]);
    int lf=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(lf,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET; sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK); sa.sin_port=htons(PORT);
    if(bind(lf,(struct sockaddr*)&sa,sizeof sa)<0){perror("bind");return 1;} listen(lf,256);
    fprintf(stderr,"[infer_server] listening on %d\n",PORT);
    for(;;){ int fd=accept(lf,0,0); if(fd<0)continue; pthread_t t; pthread_create(&t,0,conn,(void*)(long)fd); pthread_detach(t); }
    return 0; }

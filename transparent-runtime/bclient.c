#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#define REQ 256
static volatile long ok=0,bad=0; static int PORT,N,ITER;
static void* c(void*a){ (void)a; int fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in s={0}; s.sin_family=AF_INET; s.sin_port=htons(PORT); s.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(void*)&s,sizeof s)<0)return 0; unsigned char b[REQ];
    for(int i=0;i<ITER;i++){ memset(b,i,REQ); int p=0; while(p<REQ){int w=write(fd,b+p,REQ-p);if(w<=0)goto o;p+=w;}
        int g=0; while(g<REQ){int r=read(fd,b+g,REQ-g);if(r<=0)goto o;g+=r;} long cfg; memcpy(&cfg,b,8);
        if(cfg==0xABCD)__sync_fetch_and_add(&ok,1);else __sync_fetch_and_add(&bad,1);} o: close(fd); return 0; }
int main(int ac,char**av){ N=ac>1?atoi(av[1]):16; ITER=ac>2?atoi(av[2]):50; PORT=ac>3?atoi(av[3]):20560;
    pthread_t t[256]; for(int i=0;i<N;i++)pthread_create(&t[i],0,c,0); for(int i=0;i<N;i++)pthread_join(t[i],0);
    printf("ok=%ld bad=%ld\n",ok,bad); return 0; }

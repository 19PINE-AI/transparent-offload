/* A STOCK-STYLE thread-per-connection server. Plain blocking style, no async, no
 * coroutine awareness. main() accepts and spawns one pthread per connection; each
 * connection thread loops: read REQ bytes -> accel_encrypt -> write REQ bytes.
 * Compiled as a standalone binary; the transparent runtime (LD_PRELOAD) knows nothing
 * about it and interposes only standard symbols. */
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

#define REQ 4096
extern void accel_encrypt(unsigned char*buf,int n);   /* from libaccel.so */
static int PORT=20443;

static void* conn_thread(void*a){
    int fd=(int)(long)a;
    int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    unsigned char buf[REQ];
    for(;;){
        int got=0; while(got<REQ){ int r=read(fd,buf+got,REQ-got); if(r<=0) goto done; got+=r; }
        accel_encrypt(buf,REQ);                 /* the offload (synchronous library call) */
        int put=0; while(put<REQ){ int r=write(fd,buf+put,REQ-put); if(r<=0) goto done; put+=r; }
    }
done: close(fd); return 0;
}

int main(int argc,char**argv){
    if(argc>1) PORT=atoi(argv[1]);
    int lf=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(lf,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET; sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK); sa.sin_port=htons(PORT);
    if(bind(lf,(struct sockaddr*)&sa,sizeof sa)<0){ perror("bind"); return 1; }
    listen(lf,256);
    fprintf(stderr,"[conn_server] listening on %d (thread-per-connection)\n",PORT);
    for(;;){
        int fd=accept(lf,0,0); if(fd<0) continue;
        pthread_t t; pthread_create(&t,0,conn_thread,(void*)(long)fd); pthread_detach(t);
    }
    return 0;
}

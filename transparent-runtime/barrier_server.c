/* Validates fiber-aware cond + rwlock: connection handlers use rwlock (read shared config),
 * mutex+cond (increment a counter, broadcast, and cond_timedwait for it to advance). If the
 * runtime's cond/rwlock are correct, all connections complete (no hang/crash) with correct echoes. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#define REQ 256
static pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static pthread_rwlock_t rw=PTHREAD_RWLOCK_INITIALIZER;
static long counter=0, config=0xABCD;
static void* conn(void*a){ int fd=(int)(long)a; unsigned char b[REQ];
    for(;;){
        int g=0; while(g<REQ){ int r=read(fd,b+g,REQ-g); if(r<=0){close(fd);return 0;} g+=r; }
        long cfg; pthread_rwlock_rdlock(&rw); cfg=config; pthread_rwlock_unlock(&rw);   /* rwlock read */
        pthread_mutex_lock(&m);
        long mine=++counter;                       /* shared increment */
        pthread_cond_broadcast(&cv);               /* wake waiters */
        struct timespec ts; clock_gettime(CLOCK_REALTIME,&ts); ts.tv_nsec+=50000000; if(ts.tv_nsec>=1000000000){ts.tv_sec++;ts.tv_nsec-=1000000000;}
        while(counter < mine+1){ if(pthread_cond_timedwait(&cv,&m,&ts)!=0) break; }  /* wait for next, bounded */
        pthread_mutex_unlock(&m);
        memcpy(b,&cfg,8); memcpy(b+8,&mine,8);     /* echo cfg + my count (correctness) */
        int p=0; while(p<REQ){ int w=write(fd,b+p,REQ-p); if(w<=0){close(fd);return 0;} p+=w; }
    }
}
int main(int ac,char**av){ int port=ac>1?atoi(av[1]):20560;
    int lf=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(lf,SOL_SOCKET,SO_REUSEADDR,&one,4);
    struct sockaddr_in s={0}; s.sin_family=AF_INET; s.sin_addr.s_addr=htonl(INADDR_LOOPBACK); s.sin_port=htons(port);
    bind(lf,(void*)&s,sizeof s); listen(lf,128);
    fprintf(stderr,"[barrier_server] up on %d (uses cond+rwlock)\n",port);
    for(;;){ int fd=accept(lf,0,0); if(fd<0)continue; pthread_t t; pthread_create(&t,0,conn,(void*)(long)fd); pthread_detach(t); }
}

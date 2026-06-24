/* Conflict-injection app: cross-connection SHARED state with an RMW spanning the offload.
 *   read req -> [lock?] -> v=shared -> accel_encrypt (offload, YIELDS) -> shared=v+1 -> [unlock?] -> write
 * Two connections' handlers interleave at the offload. UNLOCKED -> lost updates (the hazard the
 * runtime introduces). LOCKED (pthread_mutex held across the offload) -> correct IF the runtime's
 * fiber-aware mutex parks the waiter instead of deadlocking the carrier. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#define REQ 4096
extern void accel_encrypt(unsigned char*,int);
static int PORT=20490, LOCKED=0;
static volatile long shared=0, total=0;
static pthread_mutex_t lk=PTHREAD_MUTEX_INITIALIZER;
static void* conn(void*a){ int fd=(int)(long)a; int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    unsigned char buf[REQ];
    for(;;){
        int g=0; while(g<REQ){ int r=read(fd,buf+g,REQ-g); if(r<=0) goto done; g+=r; }
        if(LOCKED) pthread_mutex_lock(&lk);
        long v=shared;             /* read shared cross-connection state */
        accel_encrypt(buf,REQ);     /* offload (YIELDS) -> another conn may interleave here */
        shared=v+1;                 /* write (RMW spans the offload) */
        if(LOCKED) pthread_mutex_unlock(&lk);
        __sync_fetch_and_add(&total,1);   /* ground-truth count (atomic) */
        int p=0; while(p<REQ){ int r=write(fd,buf+p,REQ-p); if(r<=0) goto done; p+=r; }
    }
done: close(fd); return 0; }
static void report(int s){ (void)s; fprintf(stderr,"[hostile %s] shared=%ld total=%ld lost=%ld %s\n",
    LOCKED?"LOCKED":"UNLOCKED", shared, total, total-shared, (shared==total)?"CORRECT":"CORRUPTED"); _exit(0); }
int main(int argc,char**argv){ if(argc>1)PORT=atoi(argv[1]); if(getenv("LOCKED"))LOCKED=1;
    signal(SIGINT,report); signal(SIGTERM,report);
    int lf=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(lf,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET; sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK); sa.sin_port=htons(PORT);
    if(bind(lf,(struct sockaddr*)&sa,sizeof sa)<0){perror("bind");return 1;} listen(lf,256);
    fprintf(stderr,"[hostile_server] listening on %d locked=%d\n",PORT,LOCKED);
    for(;;){ int fd=accept(lf,0,0); if(fd<0)continue; pthread_t t; pthread_create(&t,0,conn,(void*)(long)fd); pthread_detach(t); }
    return 0; }

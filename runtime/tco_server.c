/*
 * Phase 1: a REAL transparent-offload runtime over REAL TCP sockets.
 *
 * The application handler (serve()) is written in plain SYNCHRONOUS blocking style:
 *   read request -> offload(transform) -> write reply.
 * The runtime runs many connections as FastWake fibers on ONE core, driving a real
 * epoll loop. Socket I/O and the accelerator offload are BOTH yield points: a fiber
 * that would block parks and the dispatcher runs another connection. This is the
 * event-loop embodiment of "process another OS event while waiting for the accelerator."
 *
 * Modes (server side, pinned to one core):
 *   coro  - 1 thread, N fibers, offload yields (ours)
 *   block - N OS threads (one per connection), blocking I/O, offload blocks (OS switch)
 * An integrated multi-threaded client drives load and verifies the transform end-to-end.
 *
 * Offload here is EMULATED (separate device core, latency L) and applies a deterministic
 * keystream XOR so the client can verify correctness. Phase 1b swaps in real GPU AES.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "fw_fiber.h"

#define REQ 256
#define MAXCONN 4096
static int PORT=19443, NCONN=64, RUN_MS=2000, SRV_CORE=2, DEV_CORE=4;
static uint64_t L_NS=20000;

static inline uint64_t now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000ull+t.tv_nsec; }
static void pin(int c){ cpu_set_t s; CPU_ZERO(&s); CPU_SET(c,&s); sched_setaffinity(0,sizeof s,&s); }
static void set_rt(int p){ struct sched_param q; q.sched_priority=p; sched_setscheduler(0,SCHED_FIFO,&q); }
static void setnb(int fd){ int f=fcntl(fd,F_GETFL,0); fcntl(fd,F_SETFL,f|O_NONBLOCK); }
/* the offload transform the accelerator computes: keystream XOR (verifiable) */
static void transform(unsigned char*b,int n){ uint32_t k=0x9e3779b9u; for(int i=0;i<n;i++){k=k*1664525u+1013904223u; b[i]^=(unsigned char)(k>>17);} }

/* ---------- emulated accelerator (own core) ---------- */
struct slot { volatile uint64_t deadline; volatile int active, done; };
static struct slot slots[MAXCONN]; static volatile int dev_run=1;
static void* device_fn(void*a){ (void)a; pin(DEV_CORE); set_rt(90);
  while(dev_run){ uint64_t t=now_ns(); for(int i=0;i<MAXCONN;i++) if(slots[i].active&&!slots[i].done&&t>=slots[i].deadline) slots[i].done=1; }
  return 0; }

/* ---------- coroutine runtime ---------- */
enum { RUNNABLE, WAIT_IO, WAIT_OFF, DEAD };
struct conn { fw_fiber fb; int fd; int idx; int state; uint32_t io_ev; unsigned char buf[REQ]; uint32_t seq; };
static int PIPE=1, STATEFUL=0;
/* per-connection ORDER-DEPENDENT mask: response N depends on this connection's request count.
   If the runtime ever reorders a connection's requests, the client's verify fails. */
static void seqmask(unsigned char*b,int n,uint32_t s){ for(int i=0;i<n;i++){ s=s*1664525u+1013904223u; b[i]^=(unsigned char)(s>>13);} }
static struct conn *conns[MAXCONN]; static int nconns=0;
static fw_fiber sched_fb; static struct conn *cur;
static int epfd;
static volatile long g_done=0;

static void co_yield_io(struct conn*c, uint32_t ev){
    struct epoll_event e; e.events=ev|EPOLLONESHOT; e.data.ptr=c;
    epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &e);
    c->io_ev=ev; c->state=WAIT_IO; fw_fiber_switch(&c->fb,&sched_fb);
}
/* synchronous-looking blocking read of exactly n bytes; yields on EAGAIN */
static int co_read_n(struct conn*c, unsigned char*buf, int n){
    int got=0;
    while(got<n){ int r=read(c->fd,buf+got,n-got);
        if(r>0){ got+=r; continue; }
        if(r==0) return 0;                       /* peer closed */
        if(errno==EAGAIN||errno==EWOULDBLOCK){ co_yield_io(c,EPOLLIN); continue; }
        return -1; }
    return got;
}
static int co_write_n(struct conn*c, unsigned char*buf, int n){
    int put=0;
    while(put<n){ int r=write(c->fd,buf+put,n-put);
        if(r>0){ put+=r; continue; }
        if(r<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){ co_yield_io(c,EPOLLOUT); continue; }
        if(r<0) return -1; }
    return put;
}
static void co_offload(struct conn*c){
    slots[c->idx].done=0; slots[c->idx].deadline=now_ns()+L_NS; slots[c->idx].active=1;
    c->state=WAIT_OFF; fw_fiber_switch(&c->fb,&sched_fb);    /* park at offload */
    transform(c->buf, REQ);                                  /* result available on resume */
}
/* the application handler — plain synchronous style, no async/callbacks */
static void serve(void*arg){
    struct conn*c=(struct conn*)arg;
    for(;;){
        int r=co_read_n(c,c->buf,REQ); if(r<=0) break;       /* read request */
        co_offload(c);                                       /* offload (yields) */
        if(STATEFUL){ seqmask(c->buf,REQ,c->seq); c->seq++; }/* order-dependent post-processing */
        if(co_write_n(c,c->buf,REQ)<=0) break;               /* write reply */
        __sync_fetch_and_add(&g_done,1);
    }
    c->state=DEAD;
}

static struct conn* conn_new(int fd){
    struct conn*c=calloc(1,sizeof *c); c->fd=fd; c->idx=nconns; c->state=RUNNABLE;
    setnb(fd); int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    struct epoll_event e; e.events=EPOLLIN|EPOLLONESHOT; e.data.ptr=c; epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&e);
    fw_fiber_make(&c->fb, 64*1024, serve, c, &sched_fb);
    conns[nconns]=c; nconns++;
    return c;
}

static volatile int srv_stop=0;
static void run_coro(int listen_fd){
    pin(SRV_CORE); set_rt(80);
    fw_fiber_init_self(&sched_fb);
    epfd=epoll_create1(0);
    struct epoll_event le; le.events=EPOLLIN; le.data.ptr=NULL; epoll_ctl(epfd,EPOLL_CTL_ADD,listen_fd,&le);
    struct epoll_event evs[256];
    while(!srv_stop){
        /* 1: resume offload-completed fibers */
        for(int i=0;i<nconns;i++) if(conns[i]&&conns[i]->state==WAIT_OFF&&slots[conns[i]->idx].done){ conns[i]->state=RUNNABLE; slots[conns[i]->idx].active=0; }
        /* 2: poll sockets (non-blocking) */
        int n=epoll_wait(epfd,evs,256,0);
        for(int i=0;i<n;i++){
            if(evs[i].data.ptr==NULL){                       /* listener */
                for(;;){ int fd=accept(listen_fd,0,0); if(fd<0) break; if(nconns<MAXCONN) conn_new(fd); else close(fd); }
            } else { struct conn*c=evs[i].data.ptr; if(c->state==WAIT_IO) c->state=RUNNABLE; }
        }
        /* 3: run all runnable fibers one step */
        for(int i=0;i<nconns;i++){ struct conn*c=conns[i];
            if(c&&c->state==RUNNABLE){ cur=c; fw_fiber_switch(&sched_fb,&c->fb);
                if(c->state==DEAD){ epoll_ctl(epfd,EPOLL_CTL_DEL,c->fd,0); close(c->fd); conns[i]=NULL; free(c); } }
        }
    }
}

/* ---------- block mode: thread per connection, blocking I/O + blocking offload ---------- */
static void* block_conn(void*arg){
    int fd=(int)(long)arg; pin(SRV_CORE); set_rt(80);
    int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    int efd_slot = fd % MAXCONN;     /* reuse slots array for the emulated wait */
    unsigned char buf[REQ]; uint32_t seq=0;
    for(;;){
        int got=0; while(got<REQ){ int r=read(fd,buf+got,REQ-got); if(r<=0) goto done; got+=r; }
        /* blocking offload: submit then sleep-wait (OS can run other conn threads) */
        slots[efd_slot].done=0; slots[efd_slot].deadline=now_ns()+L_NS; slots[efd_slot].active=1;
        while(!slots[efd_slot].done){ struct timespec ts={0,2000}; nanosleep(&ts,0); }  /* yield core */
        slots[efd_slot].active=0; transform(buf,REQ);
        if(STATEFUL){ seqmask(buf,REQ,seq); seq++; }
        int put=0; while(put<REQ){ int r=write(fd,buf+put,REQ-put); if(r<=0) goto done; put+=r; }
        __sync_fetch_and_add(&g_done,1);
    }
done: close(fd); return 0;
}
static void run_block(int listen_fd){
    pin(SRV_CORE);
    int f=fcntl(listen_fd,F_GETFL,0); fcntl(listen_fd,F_SETFL,f&~O_NONBLOCK);  /* blocking accept */
    while(!srv_stop){ int fd=accept(listen_fd,0,0); if(fd<0) continue;
        pthread_t t; pthread_create(&t,0,block_conn,(void*)(long)fd); pthread_detach(t); }
}

/* ---------- integrated client (verifies transform end-to-end) ---------- */
static volatile int cli_stop=0; static volatile long cli_ok=0, cli_bad=0;
static uint64_t *lat_samp; static volatile long lat_n=0;
static void* client_thread(void*a){
    (void)a;
    int fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET; sa.sin_port=htons(PORT);
    sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(struct sockaddr*)&sa,sizeof sa)<0){ return 0; }
    int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    unsigned char req[REQ], rsp[REQ]; uint32_t seq=0;
    unsigned char base[REQ]; for(int i=0;i<REQ;i++) base[i]=(unsigned char)(i*131+7);
    while(!cli_stop){
        uint64_t t0=now_ns();
        /* PIPELINE: send PIPE requests back-to-back BEFORE reading any reply.
           Each request is tagged with its sequence so the expected reply is order-dependent. */
        for(int k=0;k<PIPE;k++){
            memcpy(req,base,REQ); req[0]=(unsigned char)(seq+k);   /* vary content per request */
            int put=0; while(put<REQ){ int r=write(fd,req+put,REQ-put); if(r<=0) goto out; put+=r; }
        }
        for(int k=0;k<PIPE;k++){
            int got=0; while(got<REQ){ int r=read(fd,rsp+got,REQ-got); if(r<=0) goto out; got+=r; }
            unsigned char exp[REQ]; memcpy(exp,base,REQ); exp[0]=(unsigned char)(seq+k);
            transform(exp,REQ);
            if(STATEFUL){ seqmask(exp,REQ,seq+k); }               /* must match server's per-conn order */
            if(memcmp(rsp,exp,REQ)==0) __sync_fetch_and_add(&cli_ok,1); else __sync_fetch_and_add(&cli_bad,1);
        }
        seq+=PIPE;
        long s=__sync_fetch_and_add(&lat_n,1); lat_samp[s%300000]=(now_ns()-t0)/(PIPE?PIPE:1);
    }
out: close(fd); return 0;
}

static int cmp(const void*a,const void*b){ uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b; return x<y?-1:x>y?1:0; }

static int g_listen_fd;
static void* srv_coro_fn(void*p){ (void)p; run_coro(g_listen_fd); return 0; }
static void* srv_block_fn(void*p){ (void)p; run_block(g_listen_fd); return 0; }

int main(int argc,char**argv){
    const char* mode = argc>1?argv[1]:"coro";
    if(argc>2) NCONN=atoi(argv[2]);
    if(argc>3) L_NS=strtoull(argv[3],0,10);
    if(argc>4) RUN_MS=atoi(argv[4]);
    if(argc>5) PIPE=atoi(argv[5]);
    if(argc>6) STATEFUL=atoi(argv[6]);
    lat_samp=malloc(sizeof(uint64_t)*300000);

    int lf=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(lf,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET; sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK); sa.sin_port=htons(PORT);
    if(bind(lf,(struct sockaddr*)&sa,sizeof sa)<0){ perror("bind"); return 1; }
    listen(lf,256); setnb(lf);

    pthread_t dev; pthread_create(&dev,0,device_fn,0);
    /* server in its own thread */
    pthread_t srv; int is_coro=!strcmp(mode,"coro");
    g_listen_fd=lf;
    pthread_create(&srv,0, is_coro?srv_coro_fn:srv_block_fn, 0);
    usleep(100000);
    /* clients */
    pthread_t cl[512]; for(int i=0;i<NCONN;i++) pthread_create(&cl[i],0,client_thread,0);
    uint64_t t0=now_ns(); usleep(RUN_MS*1000);
    cli_stop=1;
    for(int i=0;i<NCONN;i++) pthread_join(cl[i],0);
    double dt=(now_ns()-t0)/1e9;
    srv_stop=1; dev_run=0;
    long n=lat_n<300000?lat_n:300000; qsort(lat_samp,n,sizeof(uint64_t),cmp);
    printf("%s,NCONN=%d,L_us=%.0f,ok=%ld,bad=%ld,tput_rps=%.0f,p50_us=%.1f,p99_us=%.1f\n",
        mode,NCONN,L_NS/1000.0,cli_ok,cli_bad,(cli_ok+cli_bad)/dt,
        n?lat_samp[n/2]/1000.0:0, n?lat_samp[(long)(n*0.99)]/1000.0:0);
    return 0;
}

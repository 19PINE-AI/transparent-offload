/* libtransparent.so — LD_PRELOAD M:N fiber runtime with transparent offload.
 * Turns an UNMODIFIED thread-per-connection binary into overlapped coroutines:
 *   pthread_create  -> spawn a fiber on a carrier thread (not an OS thread)
 *   read/write      -> non-blocking + yield the fiber on EAGAIN (carrier runs peers)
 *   accel_encrypt   -> async accel_submit + yield (overlap the offload)
 * The app keeps its plain synchronous style; the runtime knows nothing app-specific.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <sys/epoll.h>
#include <time.h>
#include "fw_fiber.h"
#include "detector.h"

/* ---- real libc / libaccel symbols ---- */
static int (*real_pthread_create)(pthread_t*,const pthread_attr_t*,void*(*)(void*),void*);
static ssize_t (*real_read)(int,void*,size_t);
static ssize_t (*real_write)(int,const void*,size_t);
static int (*real_mutex_lock)(pthread_mutex_t*);
static int (*real_mutex_trylock)(pthread_mutex_t*);
static int (*real_mutex_unlock)(pthread_mutex_t*);
/* OpenSSL per-thread error-queue save/restore (resolved if libcrypto present) */
static unsigned long (*ssl_err_get)(void);
static void (*ssl_err_clear)(void);
static unsigned long (*ssl_err_peek)(void);
static void (*ssl_err_put)(int,int,int,const char*,int);
/* accelerator API resolved lazily so the runtime ALSO loads on apps without libaccel
 * (e.g. running stock nginx under the runtime as a safety test). */
static long (*accel_submit)(unsigned char*,int);
static int  (*accel_done)(long);
static void (*accel_release)(long);
static void (*real_accel_encrypt)(unsigned char*,int);
/* socket I/O used by libraries that bypass read/write (e.g. MariaDB vio: recv/send/poll) */
static ssize_t (*real_recv)(int,void*,size_t,int);
static ssize_t (*real_send)(int,const void*,size_t,int);
static ssize_t (*real_recvfrom)(int,void*,size_t,int,void*,void*);
static ssize_t (*real_sendto)(int,const void*,size_t,int,const void*,unsigned);
static int (*real_poll)(void*,unsigned long,int);

/* ---- runtime state ---- */
#define MAXFIB 8192
enum { F_NEW, F_RUN, F_WAIT_IO, F_WAIT_OFF, F_WAIT_COND, F_DONE, F_FREE };
struct fiber { fw_fiber fb; void*(*fn)(void*); void*arg; int state; int fd; long off_id; int serr; int slot;
               unsigned long errbuf[16]; int nerr; struct det_fib df; void* tls[128];
               void* waitcond; pthread_mutex_t* waitmutex; struct timespec deadline; int has_deadline; int timed_out; };
/* fiber-aware cond/rwlock plumbing */
static int (*real_cond_wait)(pthread_cond_t*,pthread_mutex_t*);
static int (*real_cond_timedwait)(pthread_cond_t*,pthread_mutex_t*,const struct timespec*);
static int (*real_cond_signal)(pthread_cond_t*);
static int (*real_cond_broadcast)(pthread_cond_t*);
static int (*real_rwlock_rdlock)(pthread_rwlock_t*);
static int (*real_rwlock_wrlock)(pthread_rwlock_t*);
static int (*real_rwlock_unlock)(pthread_rwlock_t*);
static int (*real_rwlock_tryrdlock)(pthread_rwlock_t*);
static int (*real_rwlock_trywrlock)(pthread_rwlock_t*);
/* cross-thread wake ring: real threads' cond_signal push here; the carrier drains + wakes fibers */
#define PENDN 8192
static void* pend_ring[PENDN]; static volatile long pend_head=0, pend_tail=0;
static int g_pool=0;   /* RT_POOL: fiber-ize startup threads too (thread-pool servers) */
static struct fiber* fibs[MAXFIB]; static int nfib=0;
static struct fiber* newq[MAXFIB]; static int newn=0;
static pthread_mutex_t qmx=PTHREAD_MUTEX_INITIALIZER;
static fw_fiber sched_fb;
static __thread struct fiber* cur_fib=NULL;
static int epfd;
static volatile int runtime_ready=0;
static int SRV_CORE=4;
static long g_switches=0;
static int g_realmutex=0;
static long g_conn_fibers=0;
static int g_enforce=0;                 /* RT_ENFORCE: serialize conflicting handlers */
static volatile int hlock_owner=-1;     /* fiber slot holding the handler lock, -1 = free */
static volatile int g_serving=0;        /* set after first accept(): only fiber-ize CONNECTION threads */
static int g_nextkey=1;
static int getenv_nofiber=0;
static int g_nocond=0, g_norw=0, g_notls=0, g_noself=0, g_nodetach=0, g_nomutex=0, g_noio=0, g_fiberself=0;

static void fib_entry(void*p){ struct fiber*f=p; if(f->fn) f->fn(f->arg); f->state=F_DONE; }

/* per-fiber thread-local-state save/restore (errno + OpenSSL ERR queue) at every yield.
 * Without this, all fibers on the carrier share one errno/ERR queue (the coop_err hazard). */
static void tls_save(struct fiber*f){
    f->serr=errno;
    if(ssl_err_get){ f->nerr=0; unsigned long e; while((e=ssl_err_get())!=0 && f->nerr<16) f->errbuf[f->nerr++]=e; }
}
static void tls_restore(struct fiber*f){
    errno=f->serr;
    if(ssl_err_get && ssl_err_clear && ssl_err_put){ ssl_err_clear();
        for(int i=0;i<f->nerr;i++){ int lib=(int)((f->errbuf[i]>>23)&0xFF), reason=(int)(f->errbuf[i]&0x7FFFFF);
            ssl_err_put(lib,0,reason,"",0); } }
}
/* yield current fiber back to scheduler */
static void yield_io(int fd, uint32_t ev){
    struct fiber*f=cur_fib;
    struct epoll_event e; e.events=ev|EPOLLONESHOT; e.data.ptr=f;
    if(epoll_ctl(epfd,EPOLL_CTL_MOD,fd,&e)<0) epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&e);
    f->fd=fd; f->state=F_WAIT_IO; f->has_deadline=0; tls_save(f);
    g_switches++; fw_fiber_switch(&f->fb,&sched_fb);
    tls_restore(f);
}
/* like yield_io but with a finite timeout (ms). Returns 1 if the fd became ready, 0 on timeout
 * (carrier wakes us past the deadline and sets timed_out). Used by the fiber-aware poll(). */
static int yield_io_to(int fd, uint32_t ev, int timeout_ms){
    struct fiber*f=cur_fib;
    if(timeout_ms<0){ yield_io(fd,ev); return 1; }
    struct epoll_event e; e.events=ev|EPOLLONESHOT; e.data.ptr=f;
    if(epoll_ctl(epfd,EPOLL_CTL_MOD,fd,&e)<0) epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&e);
    f->fd=fd; f->state=F_WAIT_IO; f->timed_out=0;
    clock_gettime(CLOCK_REALTIME,&f->deadline);
    f->deadline.tv_sec += timeout_ms/1000;
    f->deadline.tv_nsec += (long)(timeout_ms%1000)*1000000L;
    if(f->deadline.tv_nsec>=1000000000L){ f->deadline.tv_sec++; f->deadline.tv_nsec-=1000000000L; }
    f->has_deadline=1; tls_save(f);
    g_switches++; fw_fiber_switch(&f->fb,&sched_fb);
    tls_restore(f);
    f->has_deadline=0;
    return !f->timed_out;
}
static void yield_off(long id){
    struct fiber*f=cur_fib; f->off_id=id; f->state=F_WAIT_OFF; tls_save(f);
    det_park(&f->df);                       /* snapshot version clock + reprotect shared pages */
    g_switches++; fw_fiber_switch(&f->fb,&sched_fb);
    tls_restore(f);
}
/* lock-wait yield: stay runnable, retry on next scheduler pass (lock-respect) */
static void yield_run(void){
    struct fiber*f=cur_fib; f->state=F_RUN; tls_save(f);
    g_switches++; fw_fiber_switch(&f->fb,&sched_fb);
    tls_restore(f);
}
/* handler lock for enforcement: held from socket-read to socket-write, ACROSS the offload,
 * so a conflicting handler's read->offload->write critical section is serialized (the read
 * is now inside the lock). Fiber-aware: a waiter parks and retries. */
static void hl_acquire(struct fiber*f){ while(hlock_owner!=-1 && hlock_owner!=f->slot) yield_run(); hlock_owner=f->slot; }
static void hl_release(struct fiber*f){ if(hlock_owner==f->slot) hlock_owner=-1; }

/* wake all fibers waiting on cond c (broadcast; over-wakeup is safe — callers use predicate loops).
 * MUST be called from the carrier thread (mutates fiber state). */
static void wake_fibers(void* c){
    for(int i=0;i<nfib;i++){ struct fiber*f=fibs[i];
        if(f && f->state==F_WAIT_COND && f->waitcond==c){ f->state=F_RUN; f->waitcond=NULL; } }
}

/* ---- carrier scheduler ---- */
static void setnb(int fd){ int fl=fcntl(fd,F_GETFL,0); if(!(fl&O_NONBLOCK)) fcntl(fd,F_SETFL,fl|O_NONBLOCK); }
static void* carrier(void*a){ (void)a;
    /* RT_RT: pin + SCHED_FIFO (dedicated-core overlap, microbench mode). DEFAULT (off): leave the
     * carrier as a normal SCHED_OTHER thread. FIFO starves real OS threads that a fiber may be
     * waiting on (e.g. a mutex held by a real MariaDB thread) -> priority-inversion deadlock. */
    if(getenv("RT_RT")){
        cpu_set_t s; CPU_ZERO(&s); CPU_SET(SRV_CORE,&s); sched_setaffinity(0,sizeof s,&s);
        struct sched_param p; p.sched_priority=80; sched_setscheduler(0,SCHED_FIFO,&p);
    }
    { extern int prctl(int,...); prctl(15/*PR_SET_NAME*/,"rt-carrier"); }
    fw_fiber_init_self(&sched_fb);
    struct epoll_event evs[256];
    long lc=0;
    for(;;){
        if(++lc % 1500000 == 0)   /* periodic evidence print (~every couple seconds) */
            fprintf(stderr,"[libtransparent] conn-fibers=%ld switches=%ld faults=%ld detected-conflicts=%ld\n", g_conn_fibers, g_switches, det_faults, det_conflicts);
        /* intake new fibers */
        if(newn){ real_mutex_lock(&qmx);
            for(int i=0;i<newn;i++){ struct fiber*f=newq[i]; f->slot=nfib; fibs[nfib++]=f; }
            newn=0; real_mutex_unlock(&qmx); }
        /* I/O readiness */
        int n=epoll_wait(epfd,evs,256,0);
        for(int i=0;i<n;i++){ struct fiber*f=evs[i].data.ptr; if(f && f->state==F_WAIT_IO) f->state=F_RUN; }
        /* offload completions */
        for(int i=0;i<nfib;i++){ struct fiber*f=fibs[i]; if(f&&f->state==F_WAIT_OFF&&accel_done&&accel_done(f->off_id)){ if(accel_release)accel_release(f->off_id); f->state=F_RUN; } }
        /* cross-thread cond wakes (pushed by real threads) */
        while(pend_tail<pend_head){ void* c=pend_ring[pend_tail%PENDN]; pend_tail++; wake_fibers(c); }
        /* cond_timedwait AND poll() deadlines */
        { struct timespec now; int any=0;
          for(int i=0;i<nfib;i++){ struct fiber*f=fibs[i];
              if(f&&(f->state==F_WAIT_COND||f->state==F_WAIT_IO)&&f->has_deadline){ if(!any){clock_gettime(CLOCK_REALTIME,&now);any=1;}
              if(now.tv_sec>f->deadline.tv_sec || (now.tv_sec==f->deadline.tv_sec && now.tv_nsec>=f->deadline.tv_nsec)){
                  if(f->state==F_WAIT_IO) epoll_ctl(epfd,EPOLL_CTL_DEL,f->fd,NULL);  /* disarm pending readiness */
                  f->timed_out=1; f->state=F_RUN; f->waitcond=NULL; } } } }
        /* run runnable */
        for(int i=0;i<nfib;i++){ struct fiber*f=fibs[i];
            if(!f) continue;
            if(f->state==F_NEW||f->state==F_RUN){ cur_fib=f; det_set_fiber(&f->df); f->df.active=1; det_reprotect();
                g_switches++; fw_fiber_switch(&sched_fb,&f->fb); cur_fib=NULL;
                if(f->state==F_DONE){ fw_fiber_free(&f->fb); free(f); fibs[i]=NULL; } }
        }
    }
    return 0;
}

static volatile int carrier_running=0;
static void start_carrier(void){
    if(getenv("RT_NOCARRIER")) return;
    carrier_running=1;
    pthread_t t; real_pthread_create(&t,0,carrier,0);
}
/* MariaDB (and other daemons) fork() during startup; only the forking thread survives into the
 * child, so the carrier created in the parent is gone in the child that actually serves. Rebuild
 * runtime state and a fresh carrier in every fork child so fiber-ized connections still run. */
static void atfork_child(void){
    pthread_mutex_init(&qmx,0);          /* parent may have held it at fork */
    nfib=0; newn=0; pend_head=pend_tail=0; cur_fib=NULL; hlock_owner=-1;
    epfd=epoll_create1(0);
    carrier_running=0;
    if(runtime_ready) start_carrier();
}

/* ---- constructor: resolve symbols, warm device, start carrier ---- */
__attribute__((constructor)) static void init(void){
    real_pthread_create=dlsym(RTLD_NEXT,"pthread_create");
    real_read=dlsym(RTLD_NEXT,"read");
    real_write=dlsym(RTLD_NEXT,"write");
    real_accel_encrypt=dlsym(RTLD_NEXT,"accel_encrypt");
    real_mutex_lock=dlsym(RTLD_NEXT,"pthread_mutex_lock");
    real_mutex_trylock=dlsym(RTLD_NEXT,"pthread_mutex_trylock");
    real_mutex_unlock=dlsym(RTLD_NEXT,"pthread_mutex_unlock");
    accel_submit=dlsym(RTLD_DEFAULT,"accel_submit");
    accel_done=dlsym(RTLD_DEFAULT,"accel_done");
    accel_release=dlsym(RTLD_DEFAULT,"accel_release");
    ssl_err_get=dlsym(RTLD_DEFAULT,"ERR_get_error");      /* save/restore OpenSSL ERR */
    ssl_err_clear=dlsym(RTLD_DEFAULT,"ERR_clear_error");
    ssl_err_peek=dlsym(RTLD_DEFAULT,"ERR_peek_last_error");
    char*c=getenv("RT_CORE"); if(c) SRV_CORE=atoi(c);
    if(getenv("RT_REALMUTEX")) g_realmutex=1;
    if(getenv("RT_ENFORCE")) g_enforce=1;
    if(getenv("RT_POOL")) g_pool=1;
    if(getenv("RT_NOFIBER")) getenv_nofiber=1;
    if(getenv("RT_NOCOND")) g_nocond=1;
    if(getenv("RT_NORW")) g_norw=1;
    if(getenv("RT_NOTLS")) g_notls=1;
    if(getenv("RT_NOSELF")) g_noself=1;
    if(getenv("RT_FIBERSELF")) g_fiberself=1;
    if(getenv("RT_NODETACH")) g_nodetach=1;
    if(getenv("RT_NOMUTEX")) g_nomutex=1;
    if(getenv("RT_NOIO")) g_noio=1;
    if(getenv("RT_PASSIVE")){ g_nocond=g_norw=g_notls=g_noself=g_nodetach=g_nomutex=g_noio=1; }
    real_cond_wait=dlvsym(RTLD_NEXT,"pthread_cond_wait","GLIBC_2.3.2");
    real_cond_timedwait=dlvsym(RTLD_NEXT,"pthread_cond_timedwait","GLIBC_2.3.2");
    real_cond_signal=dlvsym(RTLD_NEXT,"pthread_cond_signal","GLIBC_2.3.2");
    real_cond_broadcast=dlvsym(RTLD_NEXT,"pthread_cond_broadcast","GLIBC_2.3.2");
    real_rwlock_rdlock=dlsym(RTLD_NEXT,"pthread_rwlock_rdlock");
    real_rwlock_wrlock=dlsym(RTLD_NEXT,"pthread_rwlock_wrlock");
    real_rwlock_unlock=dlsym(RTLD_NEXT,"pthread_rwlock_unlock");
    real_rwlock_tryrdlock=dlsym(RTLD_NEXT,"pthread_rwlock_tryrdlock");
    real_rwlock_trywrlock=dlsym(RTLD_NEXT,"pthread_rwlock_trywrlock");
    epfd=epoll_create1(0);
    /* warm the accelerator device (creates its REAL thread now, before runtime_ready) */
    if(real_accel_encrypt){ unsigned char dummy[16]={0}; real_accel_encrypt(dummy,16); }
    /* start the carrier as a REAL thread; re-establish it in fork children too */
    pthread_atfork(0,0,atfork_child);
    runtime_ready=1;
    start_carrier();
    det_init();
    fprintf(stderr,"[libtransparent] M:N runtime active (carrier core %d)\n",SRV_CORE);
}

/* ---- interposed symbols ---- */
int pthread_create(pthread_t*thread,const pthread_attr_t*attr,void*(*fn)(void*),void*arg){
    if(getenv_nofiber || !runtime_ready || (!g_serving && !g_pool) || cur_fib) {
        /* not serving yet (startup threads), or nested: REAL thread.
         * RT_POOL fiber-izes startup threads too (thread-pool servers). */
        if(!real_pthread_create) real_pthread_create=dlsym(RTLD_NEXT,"pthread_create");
        return real_pthread_create(thread,attr,fn,arg);
    }
    struct fiber*f=calloc(1,sizeof *f); f->fn=fn; f->arg=arg; f->state=F_NEW;
    fw_fiber_make(&f->fb,1024*1024,fib_entry,f,&sched_fb);
    real_mutex_lock(&qmx); if(newn<MAXFIB) newq[newn++]=f; real_mutex_unlock(&qmx);
    __sync_fetch_and_add(&g_conn_fibers,1);
    if(thread) *thread=(pthread_t)(void*)f;
    return 0;
}
int pthread_detach(pthread_t t){ static int(*r)(pthread_t); if(!r)r=dlsym(RTLD_NEXT,"pthread_detach");
    if(g_nodetach) return r(t); if(runtime_ready) return 0; return r(t); }

ssize_t read(int fd,void*buf,size_t n){
    if(g_noio){ if(!real_read)real_read=dlsym(RTLD_NEXT,"read"); return real_read(fd,buf,n);}
    if(!cur_fib){ if(!real_read)real_read=dlsym(RTLD_NEXT,"read"); return real_read(fd,buf,n); }
    if((g_enforce||det_conflicts>0) && hlock_owner!=cur_fib->slot) hl_acquire(cur_fib);  /* handler start */
    setnb(fd);
    for(;;){ ssize_t r=real_read(fd,buf,n); if(r>=0) return r;
        if(errno!=EAGAIN&&errno!=EWOULDBLOCK) return r; yield_io(fd,EPOLLIN); }
}
ssize_t write(int fd,const void*buf,size_t n){
    if(g_noio){ if(!real_write)real_write=dlsym(RTLD_NEXT,"write"); return real_write(fd,buf,n);}
    if(!cur_fib){ if(!real_write)real_write=dlsym(RTLD_NEXT,"write"); return real_write(fd,buf,n); }
    if(g_enforce||det_conflicts>0) hl_release(cur_fib);  /* response send */
    setnb(fd);
    for(;;){ ssize_t r=real_write(fd,buf,n); if(r>=0) return r;
        if(errno!=EAGAIN&&errno!=EWOULDBLOCK) return r; yield_io(fd,EPOLLOUT); }
}
/* recv/send: MariaDB's vio layer uses these (not read/write). Same yield-on-EAGAIN logic so
 * a connection-handler fiber parks at socket I/O instead of blocking the carrier OS thread. */
ssize_t recv(int fd,void*buf,size_t n,int flags){
    if(!real_recv)real_recv=dlsym(RTLD_NEXT,"recv");
    if(g_noio||!cur_fib) return real_recv(fd,buf,n,flags);
    setnb(fd);
    for(;;){ ssize_t r=real_recv(fd,buf,n,flags); if(r>=0) return r;
        if(errno!=EAGAIN&&errno!=EWOULDBLOCK) return r; yield_io(fd,EPOLLIN); }
}
ssize_t send(int fd,const void*buf,size_t n,int flags){
    if(!real_send)real_send=dlsym(RTLD_NEXT,"send");
    if(g_noio||!cur_fib) return real_send(fd,buf,n,flags);
    setnb(fd);
    for(;;){ ssize_t r=real_send(fd,buf,n,flags); if(r>=0) return r;
        if(errno!=EAGAIN&&errno!=EWOULDBLOCK) return r; yield_io(fd,EPOLLOUT); }
}
ssize_t recvfrom(int fd,void*buf,size_t n,int flags,void*sa,void*sl){
    if(!real_recvfrom)real_recvfrom=dlsym(RTLD_NEXT,"recvfrom");
    if(g_noio||!cur_fib) return real_recvfrom(fd,buf,n,flags,sa,sl);
    setnb(fd);
    for(;;){ ssize_t r=real_recvfrom(fd,buf,n,flags,sa,sl); if(r>=0) return r;
        if(errno!=EAGAIN&&errno!=EWOULDBLOCK) return r; yield_io(fd,EPOLLIN); }
}
ssize_t sendto(int fd,const void*buf,size_t n,int flags,const void*sa,unsigned sl){
    if(!real_sendto)real_sendto=dlsym(RTLD_NEXT,"sendto");
    if(g_noio||!cur_fib) return real_sendto(fd,buf,n,flags,sa,sl);
    setnb(fd);
    for(;;){ ssize_t r=real_sendto(fd,buf,n,flags,sa,sl); if(r>=0) return r;
        if(errno!=EAGAIN&&errno!=EWOULDBLOCK) return r; yield_io(fd,EPOLLOUT); }
}
/* poll: MariaDB's vio_io_wait polls a single socket fd for readiness with a timeout. In a fiber
 * we park on that fd via epoll instead of blocking the carrier. Single-fd readiness only; any
 * other poll (acceptor's multi-fd, real threads) passes through. timeout<0 => infinite park;
 * timeout>=0 finite => honored via the fiber deadline mechanism (revents=0 on expiry). */
struct rt_pollfd { int fd; short events; short revents; };
int poll(void*p,unsigned long nfds,int timeout){
    if(!real_poll)real_poll=dlsym(RTLD_NEXT,"poll");
    if(g_noio||!cur_fib||nfds!=1) return real_poll(p,nfds,timeout);
    struct rt_pollfd*fds=(struct rt_pollfd*)p;
    uint32_t ev=0; if(fds[0].events&0x001) ev|=EPOLLIN; if(fds[0].events&0x004) ev|=EPOLLOUT;
    if(!ev||timeout==0) return real_poll(p,nfds,timeout);
    int rdy=yield_io_to(fds[0].fd,ev,timeout);
    if(!rdy){ fds[0].revents=0; return 0; }          /* timed out */
    fds[0].revents=fds[0].events; return 1;
}
void accel_encrypt(unsigned char*buf,int n){
    if(!cur_fib){ if(!real_accel_encrypt)real_accel_encrypt=dlsym(RTLD_NEXT,"accel_encrypt"); real_accel_encrypt(buf,n); return; }
    long id=accel_submit(buf,n);
    yield_off(id);     /* park; carrier runs peers; resumed when device done */
}

/* fiber-aware mutex (lock-respect): a fiber blocked on a held lock PARKS and retries,
 * rather than deadlocking the carrier OS thread. Real apps that protect shared state with
 * a mutex are then correct under the runtime. */
int pthread_mutex_lock(pthread_mutex_t*m){
    if(!real_mutex_lock)real_mutex_lock=dlsym(RTLD_NEXT,"pthread_mutex_lock");
    if(g_nomutex) return real_mutex_lock(m);
    if(!real_mutex_trylock) real_mutex_trylock=dlsym(RTLD_NEXT,"pthread_mutex_trylock");
    if(!cur_fib){ if(!real_mutex_lock)real_mutex_lock=dlsym(RTLD_NEXT,"pthread_mutex_lock"); return real_mutex_lock(m); }
    if(g_realmutex) return real_mutex_lock(m);   /* naive: deadlocks the carrier */
    while(real_mutex_trylock(m)!=0) yield_run();   /* held by another fiber -> pause */
    return 0;
}
int pthread_mutex_trylock(pthread_mutex_t*m){
    if(!real_mutex_trylock) real_mutex_trylock=dlsym(RTLD_NEXT,"pthread_mutex_trylock");
    return real_mutex_trylock(m);
}
int pthread_mutex_unlock(pthread_mutex_t*m){
    if(!real_mutex_unlock) real_mutex_unlock=dlsym(RTLD_NEXT,"pthread_mutex_unlock");
    return real_mutex_unlock(m);
}

/* report whether the overlap mechanism actually engaged (evidence of the architectural boundary) */
__attribute__((destructor)) static void fini(void){
    fprintf(stderr,"[libtransparent] connection-fibers created = %ld, fiber switches = %ld\n",
            g_conn_fibers, g_switches);
}

/* ---- defer fiber-ization to serving time: interpose accept ---- */
static int (*real_accept)(int,void*,void*);
static int (*real_accept4)(int,void*,void*,int);
int accept(int fd, void* a, void* l){
    if(!real_accept) real_accept=dlsym(RTLD_NEXT,"accept");
    int r=real_accept(fd,a,l); if(r>=0) g_serving=1; return r;
}
int accept4(int fd, void* a, void* l, int fl){
    if(!real_accept4) real_accept4=dlsym(RTLD_NEXT,"accept4");
    int r=real_accept4(fd,a,l,fl); if(r>=0) g_serving=1; return r;
}
/* ---- per-fiber thread-local storage (fibers share the OS thread's real TLS) ---- */
static int (*real_key_create)(pthread_key_t*,void(*)(void*));
static int (*real_setspecific)(pthread_key_t,const void*);
static void* (*real_getspecific)(pthread_key_t);
int pthread_key_create(pthread_key_t* k, void(*d)(void*)){
    if(!real_key_create) real_key_create=dlsym(RTLD_NEXT,"pthread_key_create");
    return real_key_create(k,d);                  /* real key; we shadow per-fiber on access */
}
int pthread_setspecific(pthread_key_t k, const void* v){
    if(!g_notls && cur_fib && k<128){ cur_fib->tls[k]=(void*)v; return 0; }
    if(!real_setspecific) real_setspecific=dlsym(RTLD_NEXT,"pthread_setspecific");
    return real_setspecific(k,v);
}
void* pthread_getspecific(pthread_key_t k){
    if(!g_notls && cur_fib && k<128) return cur_fib->tls[k];
    if(!real_getspecific) real_getspecific=dlsym(RTLD_NEXT,"pthread_getspecific");
    return real_getspecific(k);
}
/* When running inside a fiber, report the FIBER's stack (not the carrier OS thread's) so apps
 * that compute stack bounds for recursion guards see the right region. MariaDB's
 * my_get_stack_bounds -> pthread_getattr_np -> pthread_attr_getstack: without this it computes a
 * garbage "stack used" (carrier base minus fiber SP) and aborts with "Thread stack overrun". */
int pthread_getattr_np(pthread_t t, pthread_attr_t* attr){
    static int(*r)(pthread_t,pthread_attr_t*); if(!r)r=dlsym(RTLD_NEXT,"pthread_getattr_np");
    int rc=r(t,attr);
    if(rc==0 && cur_fib && cur_fib->fb.stack_base)
        pthread_attr_setstack(attr, cur_fib->fb.stack_base, cur_fib->fb.stack_size);
    return rc;
}
pthread_t pthread_self(void){
    /* MUST return the real OS thread id: glibc internals (pthread_getattr_np / my_get_stack_bounds,
     * cancellation, stack unwinding) interpret pthread_self() as a real `struct pthread*`. Returning
     * a fiber pointer makes pthread_getattr_np lock-wait on garbage -> deadlock (seen in MariaDB
     * THD::store_globals). A fake per-fiber identity is opt-in only (RT_FIBERSELF), never default. */
    if(g_fiberself && cur_fib) return (pthread_t)(void*)cur_fib;
    static pthread_t (*r)(void); if(!r) r=dlsym(RTLD_NEXT,"pthread_self"); return r();
}

/* ---- fiber-aware condition variables ---- */
/* the carrier compares deadlines against CLOCK_REALTIME; convert any other clock's abstime. */
static void to_realtime(struct timespec* out, const struct timespec* abs, int clk){
    if(clk==CLOCK_REALTIME){ *out=*abs; return; }
    struct timespec nm, nr; clock_gettime(clk,&nm); clock_gettime(CLOCK_REALTIME,&nr);
    long ds=abs->tv_sec-nm.tv_sec, dn=abs->tv_nsec-nm.tv_nsec;
    out->tv_sec=nr.tv_sec+ds; out->tv_nsec=nr.tv_nsec+dn;
    if(out->tv_nsec>=1000000000L){ out->tv_sec++; out->tv_nsec-=1000000000L; }
    else if(out->tv_nsec<0){ out->tv_sec--; out->tv_nsec+=1000000000L; }
}
static int fiber_cond_wait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* abs, int clk){
    struct fiber* f=cur_fib;
    f->waitcond=c; f->waitmutex=m; f->timed_out=0;
    if(abs){ to_realtime(&f->deadline,abs,clk); f->has_deadline=1; } else f->has_deadline=0;
    f->state=F_WAIT_COND;
    pthread_mutex_unlock(m);                 /* fiber-aware unlock (releases m) */
    fw_fiber_switch(&f->fb,&sched_fb);        /* park until signalled / deadline */
    int to=f->timed_out; f->has_deadline=0;
    pthread_mutex_lock(m);                    /* re-acquire before returning (cond semantics) */
    return to;
}
int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m){
    if(g_nocond||!cur_fib){ if(!real_cond_wait)real_cond_wait=dlvsym(RTLD_NEXT,"pthread_cond_wait","GLIBC_2.3.2"); return real_cond_wait(c,m); }
    fiber_cond_wait(c,m,NULL,0); return 0;
}
int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* t){
    if(g_nocond||!cur_fib){ if(!real_cond_timedwait)real_cond_timedwait=dlvsym(RTLD_NEXT,"pthread_cond_timedwait","GLIBC_2.3.2"); return real_cond_timedwait(c,m,t); }
    return fiber_cond_wait(c,m,t,CLOCK_REALTIME)?ETIMEDOUT:0;
}
/* glibc 2.30+; used by libstdc++ std::condition_variable and MariaDB's tpool wait_for_tasks.
 * NOT versioned-dual in glibc, so plain interposition. A fiber-ized pool worker that waits here
 * must park cooperatively, else it blocks the carrier (seen: tpool worker_main fiber-ized). */
static int (*real_cond_clockwait)(pthread_cond_t*,pthread_mutex_t*,int,const struct timespec*);
int pthread_cond_clockwait(pthread_cond_t* c, pthread_mutex_t* m, int clk, const struct timespec* t){
    if(!real_cond_clockwait)real_cond_clockwait=dlsym(RTLD_NEXT,"pthread_cond_clockwait");
    if(g_nocond||!cur_fib) return real_cond_clockwait(c,m,clk,t);
    return fiber_cond_wait(c,m,t,clk)?ETIMEDOUT:0;
}
int pthread_cond_signal(pthread_cond_t* c){
    if(!real_cond_signal)real_cond_signal=dlvsym(RTLD_NEXT,"pthread_cond_signal","GLIBC_2.3.2");
    if(g_nocond) return real_cond_signal(c);
    if(0)real_cond_signal=dlvsym(RTLD_NEXT,"pthread_cond_signal","GLIBC_2.3.2");
    int r=real_cond_signal(c);                              /* wake real-thread waiters */
    if(cur_fib) wake_fibers(c);                             /* on carrier: wake fibers now */
    else if(nfib){ long h=__sync_fetch_and_add(&pend_head,1); pend_ring[h%PENDN]=c; }  /* real thread, only if fibers exist */
    return r;
}
int pthread_cond_broadcast(pthread_cond_t* c){
    if(!real_cond_broadcast)real_cond_broadcast=dlvsym(RTLD_NEXT,"pthread_cond_broadcast","GLIBC_2.3.2");
    if(g_norw&&0){} if(g_nocond) return real_cond_broadcast(c);
    if(0)real_cond_broadcast=dlvsym(RTLD_NEXT,"pthread_cond_broadcast","GLIBC_2.3.2");
    int r=real_cond_broadcast(c);
    if(cur_fib) wake_fibers(c);
    else if(nfib){ long h=__sync_fetch_and_add(&pend_head,1); pend_ring[h%PENDN]=c; }
    return r;
}

/* ---- fiber-aware rwlocks (trylock-then-park, like the fiber mutex) ---- */
int pthread_rwlock_rdlock(pthread_rwlock_t* rw){
    if(!real_rwlock_tryrdlock)real_rwlock_tryrdlock=dlsym(RTLD_NEXT,"pthread_rwlock_tryrdlock");
    if(g_norw||!cur_fib){ if(!real_rwlock_rdlock)real_rwlock_rdlock=dlsym(RTLD_NEXT,"pthread_rwlock_rdlock"); return real_rwlock_rdlock(rw); }
    while(real_rwlock_tryrdlock(rw)!=0) yield_run(); return 0;
}
int pthread_rwlock_wrlock(pthread_rwlock_t* rw){
    if(!real_rwlock_trywrlock)real_rwlock_trywrlock=dlsym(RTLD_NEXT,"pthread_rwlock_trywrlock");
    if(g_norw||!cur_fib){ if(!real_rwlock_wrlock)real_rwlock_wrlock=dlsym(RTLD_NEXT,"pthread_rwlock_wrlock"); return real_rwlock_wrlock(rw); }
    while(real_rwlock_trywrlock(rw)!=0) yield_run(); return 0;
}
int pthread_rwlock_unlock(pthread_rwlock_t* rw){
    if(!real_rwlock_unlock)real_rwlock_unlock=dlsym(RTLD_NEXT,"pthread_rwlock_unlock"); return real_rwlock_unlock(rw);
}
int pthread_rwlock_tryrdlock(pthread_rwlock_t* rw){
    if(!real_rwlock_tryrdlock)real_rwlock_tryrdlock=dlsym(RTLD_NEXT,"pthread_rwlock_tryrdlock"); return real_rwlock_tryrdlock(rw); }
int pthread_rwlock_trywrlock(pthread_rwlock_t* rw){
    if(!real_rwlock_trywrlock)real_rwlock_trywrlock=dlsym(RTLD_NEXT,"pthread_rwlock_trywrlock"); return real_rwlock_trywrlock(rw); }

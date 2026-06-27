/* spoa_agent.c — a minimal C SPOA (Stream Processing Offload Agent) for HAProxy's
 * SPOE, with NO GIL: NOTIFY frames are dispatched to a real pthread worker pool so
 * offloads overlap. Each request runs a real GPU AES offload (libaccel via ACCEL_LIB).
 * Implements just enough SPOP/2.0: HELLO handshake (advertise pipelining) + NOTIFY->ACK.
 *
 *   env: ACCEL_LIB, ACCEL_AES_BYTES, SPOA_WORKERS, HSM unused; listens on PORT (argv1|9002)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

static void (*accel_encrypt)(unsigned char*, int);
static int PORT = 9002;

/* ---- SPOP frame types ---- */
#define FT_HAPROXY_HELLO 1
#define FT_HAPROXY_DISCON 2
#define FT_NOTIFY 3
#define FT_AGENT_HELLO 101
#define FT_AGENT_DISCON 102
#define FT_ACK 103
#define FLAG_FIN 0x00000001U

static int readn(int fd, void *b, int n){ char*p=b; int g=0; while(g<n){ int r=read(fd,p+g,n-g); if(r<=0) return -1; g+=r; } return 0; }
static int writen(int fd, const void *b, int n){ const char*p=b; int s=0; while(s<n){ int r=write(fd,p+s,n-s); if(r<=0) return -1; s+=r; } return 0; }

/* SPOP varint */
static int enc_varint(unsigned char*o, uint64_t v){
    if (v < 240){ o[0]=(unsigned char)v; return 1; }
    int i=0; o[i++]=(unsigned char)(v|240); v=(v-240)>>4;
    while (v>=128){ o[i++]=(unsigned char)(v|128); v=(v-128)>>7; }
    o[i++]=(unsigned char)v; return i;
}
static uint64_t dec_varint(const unsigned char*b, int*off){
    uint64_t b0=b[(*off)++]; if (b0<240) return b0;
    uint64_t v=b0; int r=4, x;
    do { x=b[(*off)++]; v+=(uint64_t)x<<r; r+=7; } while (x>=128);
    return v;
}
/* encode key string (varint len + bytes) */
static int enc_str(unsigned char*o, const char*s){
    int n=strlen(s); int k=enc_varint(o,n); memcpy(o+k,s,n); return k+n;
}

/* per-connection write lock (ACK frames serialized; offloads run in parallel) */
struct conn { int fd; pthread_mutex_t wlk; };

/* worker pool over a job queue of (conn, stream_id, frame_id) */
#define QN 65536
struct job { struct conn*c; uint64_t sid, fid; };
static struct job q[QN];
static volatile long qh=0, qt=0;
static pthread_mutex_t qmx=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  qcv=PTHREAD_COND_INITIALIZER;

static void send_ack(struct conn*c, uint64_t sid, uint64_t fid){
    unsigned char p[64]; int n=0;
    p[n++]=FT_ACK;
    uint32_t fl=htonl(FLAG_FIN); memcpy(p+n,&fl,4); n+=4;
    n+=enc_varint(p+n,sid); n+=enc_varint(p+n,fid);
    /* empty ACTIONS list payload */
    unsigned char frame[80]; uint32_t len=htonl(n); memcpy(frame,&len,4); memcpy(frame+4,p,n);
    pthread_mutex_lock(&c->wlk); writen(c->fd, frame, 4+n); pthread_mutex_unlock(&c->wlk);
}

static void* worker(void*a){ (void)a; unsigned char buf[4096];
    for(;;){
        pthread_mutex_lock(&qmx);
        while(qh==qt) pthread_cond_wait(&qcv,&qmx);
        struct job j=q[qt%QN]; qt++;
        pthread_mutex_unlock(&qmx);
        accel_encrypt(buf, 4096);          /* REAL offload, off the GIL-free C path */
        send_ack(j.c, j.sid, j.fid);
    }
    return 0;
}

static void send_agent_hello(struct conn*c, uint32_t mfs){
    unsigned char p[256]; int n=0;
    p[n++]=FT_AGENT_HELLO;
    uint32_t fl=htonl(FLAG_FIN); memcpy(p+n,&fl,4); n+=4;
    n+=enc_varint(p+n,0); n+=enc_varint(p+n,0);          /* stream-id=0, frame-id=0 */
    n+=enc_str(p+n,"version");      p[n++]=0x08; n+=enc_str(p+n,"2.0");
    n+=enc_str(p+n,"max-frame-size");p[n++]=0x03; n+=enc_varint(p+n,mfs);
    n+=enc_str(p+n,"capabilities"); p[n++]=0x08; n+=enc_str(p+n,"pipelining");
    unsigned char frame[300]; uint32_t len=htonl(n); memcpy(frame,&len,4); memcpy(frame+4,p,n);
    pthread_mutex_lock(&c->wlk); writen(c->fd, frame, 4+n); pthread_mutex_unlock(&c->wlk);
}

static void* conn_thread(void*a){
    struct conn*c=a; int one=1; setsockopt(c->fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    unsigned char hdr[4]; unsigned char*pl=malloc(1<<20);
    for(;;){
        if (readn(c->fd,hdr,4)<0) break;
        uint32_t flen; memcpy(&flen,hdr,4); flen=ntohl(flen);
        if (flen==0 || flen> (1<<20)) break;
        if (readn(c->fd,pl,flen)<0) break;
        int off=0; int type=pl[off++];
        off+=4;                                  /* flags */
        uint64_t sid=dec_varint(pl,&off), fid=dec_varint(pl,&off);
        if (type==FT_HAPROXY_HELLO){
            send_agent_hello(c, 16380);
        } else if (type==FT_NOTIFY){
            pthread_mutex_lock(&qmx); q[qh%QN].c=c; q[qh%QN].sid=sid; q[qh%QN].fid=fid; qh++;
            pthread_cond_signal(&qcv); pthread_mutex_unlock(&qmx);
        } else if (type==FT_HAPROXY_DISCON){ break; }
    }
    free(pl); close(c->fd); free(c); return 0;
}

int main(int argc,char**argv){
    if (argc>1) PORT=atoi(argv[1]);
    char*lib=getenv("ACCEL_LIB"); if(!lib) lib="libaccel.so";
    void*h=dlopen(lib,RTLD_NOW|RTLD_GLOBAL); if(!h){ fprintf(stderr,"dlopen %s failed\n",lib); return 1; }
    accel_encrypt=dlsym(h,"accel_encrypt"); if(!accel_encrypt){ fprintf(stderr,"no accel_encrypt\n"); return 1; }
    int nw=64; char*e=getenv("SPOA_WORKERS"); if(e) nw=atoi(e);
    for(int i=0;i<nw;i++){ pthread_t t; pthread_create(&t,0,worker,0); pthread_detach(t); }

    int lf=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(lf,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET;
    sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK); sa.sin_port=htons(PORT);
    if (bind(lf,(struct sockaddr*)&sa,sizeof sa)<0){ perror("bind"); return 1; }
    listen(lf,256);
    fprintf(stderr,"[spoa_agent.c] C SPOA on 127.0.0.1:%d, %d workers\n",PORT,nw);
    for(;;){ int fd=accept(lf,0,0); if(fd<0) continue;
        struct conn*c=malloc(sizeof*c); c->fd=fd; pthread_mutex_init(&c->wlk,0);
        pthread_t t; pthread_create(&t,0,conn_thread,c); pthread_detach(t); }
    return 0;
}

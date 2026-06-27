/* libaccel_remote.so — same accel_* API, but the offload is a REAL remote round-trip
 * to remote_hsm (a real RSA-2048 signer over TCP). No GPU, no emulated latency: the
 * offload's latency is a genuine network round-trip + real remote crypto, and it is
 * latency-bound, so many can be in flight (overlap up to the remote's real concurrency).
 *
 *   accel_encrypt(buf,n) — blocking RPC: send 32-byte digest, recv 256-byte signature.
 * Each calling thread keeps its own persistent connection (thread-local socket), so a
 * server's worker pool drives many concurrent RPCs. Env: ACCEL_REMOTE_HOST/PORT.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

static int PORT = 7900;
static unsigned long HOST = 0x7f000001; /* 127.0.0.1 */
static __thread int sock = -1;

static void cfg(void){
    char*p=getenv("ACCEL_REMOTE_PORT"); if(p) PORT=atoi(p);
    char*h=getenv("ACCEL_REMOTE_HOST"); if(h){ struct in_addr a; if(inet_aton(h,&a)) HOST=ntohl(a.s_addr); }
}
static int connect_remote(void){
    int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return -1;
    int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET;
    sa.sin_addr.s_addr=htonl((unsigned)HOST); sa.sin_port=htons(PORT);
    if(connect(fd,(struct sockaddr*)&sa,sizeof sa)<0){ close(fd); return -1; }
    return fd;
}
static int rw_all(int fd,const void*b,int n,int wr){ const char*p=b; int d=0;
    while(d<n){ int r= wr? write(fd,p+d,n-d) : read(fd,(char*)p+d,n-d); if(r<=0) return -1; d+=r; } return 0; }

/* synchronous offload: one real remote RSA signature round-trip */
void accel_encrypt(unsigned char *buf, int n){
    (void)n;
    if (sock<0){ cfg(); sock=connect_remote(); if(sock<0) return; }
    unsigned char digest[32], sig[256];
    memcpy(digest, buf, 32);                       /* a real digest derived from the request */
    if (rw_all(sock, digest, 32, 1) < 0){ close(sock); sock=connect_remote(); if(sock<0) return; rw_all(sock,digest,32,1); }
    rw_all(sock, sig, 256, 0);                      /* wait for the real signature */
    memcpy(buf, sig, 32<n?32:n);
}

/* async API kept for symmetry; the integrations drive accel_encrypt on a worker pool. */
long accel_submit(unsigned char *buf, int n){ accel_encrypt(buf,n); return 0; }
int  accel_done(long id){ (void)id; return 1; }
void accel_release(long id){ (void)id; }

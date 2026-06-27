/* remote_hsm.c — a REAL remote offload service (models an HSM / remote signer).
 * TCP server; each request triggers a REAL RSA-2048 private-key signature (OpenSSL),
 * genuinely ~sub-ms of real crypto, replied over the socket. No sleeps, no emulation:
 * the latency is a real network round-trip + real asymmetric crypto. Thread-per-
 * connection, so the device's effective queue depth is the number of client links
 * scheduled across the host's cores.
 *
 *   Request  : 32 bytes (a digest to sign)
 *   Response : 256 bytes (the RSA-2048 signature)
 *   Env: HSM_PORT (default 7900)
 */
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
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

static EVP_PKEY *KEY;                 /* shared RSA-2048 key (sign is thread-safe per-ctx) */
static int PORT = 7900;

static int readn(int fd, void *b, int n){ char*p=b; int g=0; while(g<n){ int r=read(fd,p+g,n-g); if(r<=0) return -1; g+=r; } return 0; }
static int writen(int fd, const void *b, int n){ const char*p=b; int s=0; while(s<n){ int r=write(fd,p+s,n-s); if(r<=0) return -1; s+=r; } return 0; }

static void* conn(void *a){
    int fd = (int)(long)a; int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    unsigned char digest[32], sig[512];
    for(;;){
        if (readn(fd, digest, sizeof digest) < 0) break;
        size_t siglen = sizeof sig;
        EVP_PKEY_CTX *c = EVP_PKEY_CTX_new(KEY, NULL);     /* REAL RSA-2048 sign */
        EVP_PKEY_sign_init(c);
        EVP_PKEY_CTX_set_signature_md(c, EVP_sha256());
        EVP_PKEY_sign(c, sig, &siglen, digest, sizeof digest);
        EVP_PKEY_CTX_free(c);
        if (writen(fd, sig, 256) < 0) break;               /* RSA-2048 sig = 256 bytes */
    }
    close(fd); return 0;
}

int main(int argc, char**argv){
    if (argc>1) PORT=atoi(argv[1]); char*e=getenv("HSM_PORT"); if(e) PORT=atoi(e);
    EVP_PKEY_CTX *g = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    EVP_PKEY_keygen_init(g); EVP_PKEY_CTX_set_rsa_keygen_bits(g, 2048);
    EVP_PKEY_keygen(g, &KEY); EVP_PKEY_CTX_free(g);

    int lf=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(lf,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in sa; memset(&sa,0,sizeof sa); sa.sin_family=AF_INET;
    sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK); sa.sin_port=htons(PORT);
    if (bind(lf,(struct sockaddr*)&sa,sizeof sa)<0){ perror("bind"); return 1; }
    listen(lf, 1024);
    fprintf(stderr,"[remote_hsm] RSA-2048 signer on 127.0.0.1:%d\n", PORT);
    for(;;){ int fd=accept(lf,0,0); if(fd<0) continue;
        pthread_t t; pthread_create(&t,0,conn,(void*)(long)fd); pthread_detach(t); }
    return 0;
}

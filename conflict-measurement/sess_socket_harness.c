/*
 * Session-cache conflict test over REAL TCP sockets (fixes the BIO-pair gap where the
 * internal cache never populated). Models the transparent-coroutine interleaving as the
 * TSan thread model: N server threads share one SSL_CTX whose INTERNAL session cache is
 * concurrently written (fresh handshakes -> SSL_CTX_add_session) and read (resumptions ->
 * SSL_CTX_get_session). Any TSan race == an unlocked cross-connection conflict on the
 * session cache that the fd-partition + lock-respecting scheme would NOT catch.
 *
 * TLS1.2 + SSL_OP_NO_TICKET + SSL_SESS_CACHE_SERVER => stateful session-ID resumption
 * through the internal cache (exactly nginx `ssl_session_cache builtin; ssl_session_tickets off`).
 */
#define _GNU_SOURCE
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

static SSL_CTX *server_ctx, *client_ctx;
static int PORT=18443, NSERV=8, NCLI=8, listen_fd;
static volatile int stop=0;
static long g_accept=0, g_resumed=0;
static int do_resume=1;

static void die(const char*m){ fprintf(stderr,"FATAL %s: ",m); ERR_print_errors_fp(stderr); exit(1); }

static void *server_thread(void *a){
    (void)a;
    while(!__atomic_load_n(&stop,__ATOMIC_RELAXED)){
        struct sockaddr_in cli; socklen_t cl=sizeof cli;
        int fd=accept(listen_fd,(struct sockaddr*)&cli,&cl);
        if(fd<0) continue;
        int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
        SSL *s=SSL_new(server_ctx);            /* shares server_ctx -> shared internal cache */
        SSL_set_fd(s,fd);
        if(SSL_accept(s)==1){                  /* full handshake: add/lookup in the cache */
            __sync_fetch_and_add(&g_accept,1);
            char b[64]; SSL_read(s,b,sizeof b);
            SSL_write(s,"ok",2);
        }
        SSL_shutdown(s); SSL_free(s); close(fd);
    }
    return 0;
}

static void *client_thread(void *a){
    (void)a;
    SSL_SESSION *saved=NULL;
    while(!__atomic_load_n(&stop,__ATOMIC_RELAXED)){
        int fd=socket(AF_INET,SOCK_STREAM,0);
        struct sockaddr_in sa; memset(&sa,0,sizeof sa);
        sa.sin_family=AF_INET; sa.sin_port=htons(PORT); inet_pton(AF_INET,"127.0.0.1",&sa.sin_addr);
        if(connect(fd,(struct sockaddr*)&sa,sizeof sa)<0){ close(fd); continue; }
        int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
        SSL *c=SSL_new(client_ctx); SSL_set_fd(c,fd);
        if(do_resume && saved) SSL_set_session(c,saved);  /* present prior session -> cache READ */
        if(SSL_connect(c)==1){
            if(SSL_session_reused(c)) __sync_fetch_and_add(&g_resumed,1);
            SSL_write(c,"hi",2); char b[64]; SSL_read(c,b,sizeof b);
            SSL_SESSION *ns=SSL_get1_session(c);
            if(ns){ if(saved) SSL_SESSION_free(saved); saved=ns; }   /* keep newest for resumption */
        }
        SSL_shutdown(c); SSL_free(c); close(fd);
    }
    if(saved) SSL_SESSION_free(saved);
    return 0;
}

int main(int argc,char**argv){
    if(argc>1) NSERV=atoi(argv[1]);
    if(argc>2) NCLI=atoi(argv[2]);
    int run_ms = argc>3? atoi(argv[3]) : 2000;
    do_resume = argc>4? atoi(argv[4]) : 1;
    SSL_library_init(); SSL_load_error_strings();
    server_ctx=SSL_CTX_new(TLS_server_method()); client_ctx=SSL_CTX_new(TLS_client_method());
    if(SSL_CTX_use_certificate_file(server_ctx,"eccert.pem",SSL_FILETYPE_PEM)<=0) die("cert");
    if(SSL_CTX_use_PrivateKey_file(server_ctx,"eckey.pem",SSL_FILETYPE_PEM)<=0) die("key");
    SSL_CTX_set_verify(client_ctx,SSL_VERIFY_NONE,0);
    /* nginx `ssl_session_cache builtin; ssl_session_tickets off; ssl_protocols TLSv1.2` */
    SSL_CTX_set_session_cache_mode(server_ctx, SSL_SESS_CACHE_SERVER);
    SSL_CTX_set_min_proto_version(server_ctx,TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(server_ctx,TLS1_2_VERSION);
    SSL_CTX_set_options(server_ctx, SSL_OP_NO_TICKET);
    SSL_CTX_sess_set_cache_size(server_ctx, 128);   /* small -> add+evict churn */
    SSL_CTX_set_session_cache_mode(client_ctx, SSL_SESS_CACHE_CLIENT);
    SSL_CTX_set_min_proto_version(client_ctx,TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(client_ctx,TLS1_2_VERSION);
    SSL_CTX_set_options(client_ctx, SSL_OP_NO_TICKET);

    listen_fd=socket(AF_INET,SOCK_STREAM,0);
    int one=1; setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in sa; memset(&sa,0,sizeof sa);
    sa.sin_family=AF_INET; sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK); sa.sin_port=htons(PORT);
    if(bind(listen_fd,(struct sockaddr*)&sa,sizeof sa)<0) die("bind");
    if(listen(listen_fd,128)<0) die("listen");

    fprintf(stderr,"[sess-socket NSERV=%d NCLI=%d run_ms=%d port=%d]\n",NSERV,NCLI,run_ms,PORT);
    pthread_t sv[64], cl[64];
    for(int i=0;i<NSERV;i++) pthread_create(&sv[i],0,server_thread,0);
    for(int i=0;i<NCLI;i++) pthread_create(&cl[i],0,client_thread,0);
    usleep(run_ms*1000);
    __atomic_store_n(&stop,1,__ATOMIC_RELAXED);
    /* unblock accept() threads */
    for(int i=0;i<NSERV;i++){ int f=socket(AF_INET,SOCK_STREAM,0); connect(f,(struct sockaddr*)&sa,sizeof sa); close(f); }
    for(int i=0;i<NCLI;i++) pthread_join(cl[i],0);
    for(int i=0;i<NSERV;i++) pthread_join(sv[i],0);

    fprintf(stderr,"[done accepts=%ld resumptions=%ld sess_number=%ld sess_accept=%ld sess_hit=%ld]\n",
        g_accept, g_resumed, SSL_CTX_sess_number(server_ctx),
        SSL_CTX_sess_accept(server_ctx), SSL_CTX_sess_hits(server_ctx));
    return 0;
}

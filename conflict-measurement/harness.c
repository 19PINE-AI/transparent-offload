/*
 * conflict-frequency measurement for "transparent offload" on the TLS crypto path.
 *
 * Model: a transparent-coroutine runtime parks an event handler at its crypto
 * "offload" call and runs another connection's handler meanwhile. fd-partitioning
 * serializes same-connection events; the residual hazard is UNLOCKED cross-connection
 * shared state touched during the interleaved (post-)processing.
 *
 * That residual hazard set == the set of data races when two DIFFERENT connections'
 * server-side handlers run concurrently with no synchronization, sharing one SSL_CTX,
 * EXCLUDING anything OpenSSL already protects with a lock (the coroutine scheme
 * respects locks: "if a coroutine is blocked by a lock held by another, it is paused").
 *
 * ThreadSanitizer reports exactly that: unsynchronized shared read/write (>=1 write),
 * automatically ignoring per-connection/stack state and lock-protected state.
 *
 * Each worker thread drives its OWN connections (own SSL objects + own in-memory
 * BIO transport). Threads share only the SSL_CTX and OpenSSL global/library state.
 * So every race TSan reports is a genuine cross-connection crypto-path conflict.
 *
 * Modes:
 *   handshake  - each thread loops: new conn, full TLS handshake, free.
 *                (asymmetric "offload"; exercises session cache insert, RNG, stats)
 *   data       - connections pre-established single-threaded; threads then loop
 *                record exchanges only. Isolates the symmetric bulk "offload":
 *                AES-GCM record en/decrypt + per-conn seqno + post-processing.
 *   poscontrol - like data, but each post-processing step bumps an unlocked shared
 *                counter. TSan MUST flag it -> proves the harness really overlaps
 *                handlers in a conflict-exposing way (guards against false "0 races").
 */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/rand.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static SSL_CTX *server_ctx;   /* shared across all connections, like an nginx worker */
static SSL_CTX *client_ctx;
static const char *CERT = "eccert.pem", *KEY = "eckey.pem";  /* ECDSA: faster under TSan */
static int   N_THREADS = 4;
static int   N_ITERS   = 50;     /* per-thread handshakes or record exchanges */
static volatile long shared_counter = 0;   /* poscontrol only */
static int   POSCONTROL = 0;

/* one TLS connection entirely in memory: client SSL <-> server SSL via two mem BIOs */
struct conn {
    SSL *cli, *srv;
};

static void die(const char *m){ fprintf(stderr,"FATAL: %s\n",m); ERR_print_errors_fp(stderr); exit(1); }

static struct conn *conn_new(void){
    struct conn *c = calloc(1, sizeof *c);
    c->cli = SSL_new(client_ctx);
    c->srv = SSL_new(server_ctx);
    if(!c->cli||!c->srv) die("SSL_new");
    SSL_set_connect_state(c->cli);
    SSL_set_accept_state(c->srv);
    /* two shared in-memory transport buffers:
       cw: client-out / server-in   sw: server-out / client-in */
    BIO *cw=BIO_new(BIO_s_mem()), *sw=BIO_new(BIO_s_mem());
    if(!cw||!sw) die("BIO_new");
    /* each buffer is referenced by two SSLs (one rbio slot, one wbio slot) ->
       give the second reference so SSL_free refcounting stays balanced. */
    BIO_up_ref(cw); BIO_up_ref(sw);
    SSL_set_bio(c->cli, sw /*rbio*/, cw /*wbio*/);  /* client reads sw, writes cw */
    SSL_set_bio(c->srv, cw /*rbio*/, sw /*wbio*/);  /* server reads cw, writes sw */
    return c;
}

/* drive handshake to completion: bytes flow through the shared BIOs as we step ends */
static void do_handshake(struct conn *c){
    for(int i=0;i<64;i++){
        int rc_c = SSL_do_handshake(c->cli);
        int rc_s = SSL_do_handshake(c->srv);
        if(SSL_is_init_finished(c->cli) && SSL_is_init_finished(c->srv)) return;
        /* with in-memory BIOs, an empty buffer can surface as WANT_READ or a
           benign SYSCALL; only a real protocol error (SSL_ERROR_SSL) is fatal. */
        if(SSL_get_error(c->cli, rc_c)==SSL_ERROR_SSL) die("cli handshake");
        if(SSL_get_error(c->srv, rc_s)==SSL_ERROR_SSL) die("srv handshake");
    }
    die("handshake did not finish");
}

/* one application record exchange; server side does the bulk AES-GCM crypto =
 * the symmetric "offload" + its post-processing. */
static void do_exchange(struct conn *c){
    unsigned char req[256], resp[256], buf[512];
    memset(req,'q',sizeof req);
    if(SSL_write(c->cli, req, sizeof req)<=0) die("cli write");
    int n = SSL_read(c->srv, buf, sizeof buf);          /* server bulk decrypt + post */
    if(n<=0) die("srv read");
    if(POSCONTROL) shared_counter += n;                  /* planted unlocked shared write */
    memset(resp,'r',sizeof resp);
    if(SSL_write(c->srv, resp, sizeof resp)<=0) die("srv write");  /* server bulk encrypt */
    if(SSL_read(c->cli, buf, sizeof buf)<=0) die("cli read");
}

static void conn_free(struct conn *c){ SSL_free(c->cli); SSL_free(c->srv); free(c); }

/* ---- worker bodies ---- */
static void *worker_handshake(void *arg){
    for(int i=0;i<N_ITERS;i++){
        struct conn *c = conn_new();
        do_handshake(c);
        conn_free(c);
    }
    (void)arg; return 0;
}

static struct conn **pre;   /* pre-established conns for data/poscontrol mode */
static void *worker_data(void *arg){
    long id=(long)arg;
    struct conn *c = pre[id];
    for(int i=0;i<N_ITERS;i++)
        do_exchange(c);             /* the measured cross-conn window */
    return 0;
}

int main(int argc, char**argv){
    const char *mode = argc>1? argv[1] : "data";
    if(argc>2) N_THREADS = atoi(argv[2]);
    if(argc>3) N_ITERS   = atoi(argv[3]);
    if(!strcmp(mode,"poscontrol")) POSCONTROL=1;
    int is_hs13 = !strcmp(mode,"handshake");      /* TLS1.3 default: stateless tickets */
    int is_hs12 = !strcmp(mode,"handshake12");    /* TLS1.2 + no ticket: stateful internal cache */
    int is_handshake = is_hs13 || is_hs12;

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    server_ctx = SSL_CTX_new(TLS_server_method());
    client_ctx = SSL_CTX_new(TLS_client_method());
    if(!server_ctx||!client_ctx) die("CTX_new");
    if(SSL_CTX_use_certificate_file(server_ctx,CERT,SSL_FILETYPE_PEM)<=0) die("cert");
    if(SSL_CTX_use_PrivateKey_file(server_ctx,KEY,SSL_FILETYPE_PEM)<=0) die("key");
    SSL_CTX_set_verify(client_ctx, SSL_VERIFY_NONE, 0);
    /* server-side session cache ON, like nginx ssl_session_cache builtin:
       the prime cross-connection shared-state suspect on the handshake path. */
    SSL_CTX_set_session_cache_mode(server_ctx, SSL_SESS_CACHE_SERVER);
    if(is_hs12){
        /* force the STATEFUL internal session cache: TLS1.2, tickets off, and a
           small cache so inserts trigger evictions -> max churn on the shared
           LHASH + LRU list (the real cross-connection mutable structure). */
        SSL_CTX_set_min_proto_version(server_ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(server_ctx, TLS1_2_VERSION);
        SSL_CTX_set_options(server_ctx, SSL_OP_NO_TICKET);
        SSL_CTX_sess_set_cache_size(server_ctx, 64);
    }

    fprintf(stderr,"[mode=%s threads=%d iters=%d poscontrol=%d]\n",
            mode,N_THREADS,N_ITERS,POSCONTROL);

    int prewarm = (argc>4)? atoi(argv[4]) : 0;
    if(prewarm){
        /* populate lazy global caches (provider/property algorithm-fetch store,
           etc.) single-threaded before serving, then they are read-only */
        for(int i=0;i<3;i++){ struct conn *c=conn_new(); do_handshake(c); conn_free(c); }
        fprintf(stderr,"[prewarmed %d handshakes single-threaded]\n", 3);
    }

    pthread_t th[256];
    if(is_handshake){
        for(long i=0;i<N_THREADS;i++)
            if(pthread_create(&th[i],0,worker_handshake,(void*)i)) die("pthread_create");
    } else {
        /* establish all connections single-threaded so only record processing races */
        pre = calloc(N_THREADS, sizeof *pre);
        for(int i=0;i<N_THREADS;i++){ pre[i]=conn_new(); do_handshake(pre[i]); }
        for(long i=0;i<N_THREADS;i++)
            if(pthread_create(&th[i],0,worker_data,(void*)i)) die("pthread_create");
    }
    for(int i=0;i<N_THREADS;i++) pthread_join(th[i],0);

    /* prove the shared session cache (prime cross-conn suspect) was exercised */
    fprintf(stderr,"[ctx sess_number=%ld sess_accept=%ld accept_good=%ld sess_hit=%ld]\n",
            SSL_CTX_sess_number(server_ctx), SSL_CTX_sess_accept(server_ctx),
            SSL_CTX_sess_accept_good(server_ctx), SSL_CTX_sess_hits(server_ctx));
    fprintf(stderr,"[done mode=%s counter=%ld]\n", mode, shared_counter);
    return 0;
}

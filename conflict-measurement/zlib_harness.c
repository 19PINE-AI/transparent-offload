/*
 * Conflict measurement for the COMPRESSION offload class (zlib), same TSan method
 * as the crypto path: worker threads = interleaved connection handlers sharing
 * process/library state; any race == an unlocked cross-connection conflict that
 * fd-partitioning + lock-respecting would NOT catch.
 *
 * The offloaded routine is deflate() (the heavy compression). We test the
 * realistic server patterns and ask where cross-connection conflicts come from:
 *
 *   pure        - each handler owns its z_stream, compresses independent data.
 *                 (the normal nginx-gzip / per-object storage compression case)
 *   shareddict  - all handlers deflateSetDictionary() from one shared, read-only
 *                 preset dictionary (trained-dictionary storage compression).
 *   stats_nolock- pure + each handler updates a shared stats struct with NO lock
 *                 (a naive app-level aggregate in post-processing).
 *   stats_lock  - pure + the same shared stats under a mutex.
 */
#include <zlib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int N_THREADS = 8, N_ITERS = 2000;
static int MODE_DICT=0, MODE_STATS=0, MODE_LOCK=0;
static const unsigned char *DICT; static unsigned DICTLEN;
static unsigned char *SRC; static unsigned SRCLEN;
static struct { long in, out, count; } stats;     /* shared app-level aggregate */
static pthread_mutex_t stats_mtx = PTHREAD_MUTEX_INITIALIZER;

/* one event: compress one independent buffer = the deflate "offload" + post */
static void handle(unsigned char *out, unsigned outcap){
    z_stream s; memset(&s,0,sizeof s);
    if (deflateInit(&s, 6) != Z_OK){ fprintf(stderr,"deflateInit\n"); exit(1);}
    if (MODE_DICT) deflateSetDictionary(&s, DICT, DICTLEN);   /* shared read-only dict */
    s.next_in = SRC; s.avail_in = SRCLEN;
    s.next_out = out; s.avail_out = outcap;
    if (deflate(&s, Z_FINISH) != Z_STREAM_END){ fprintf(stderr,"deflate\n"); exit(1);}
    unsigned produced = outcap - s.avail_out;
    deflateEnd(&s);
    /* post-processing: app-level aggregate (the cross-connection state under test) */
    if (MODE_STATS){
        if (MODE_LOCK) pthread_mutex_lock(&stats_mtx);
        stats.in    += SRCLEN;
        stats.out   += produced;
        stats.count += 1;
        if (MODE_LOCK) pthread_mutex_unlock(&stats_mtx);
    }
}

static void *worker(void *arg){
    (void)arg;
    unsigned cap = compressBound(SRCLEN) + 64;
    unsigned char *out = malloc(cap);
    for (int i=0;i<N_ITERS;i++) handle(out, cap);
    free(out);
    return 0;
}

int main(int argc, char**argv){
    const char *mode = argc>1?argv[1]:"pure";
    if (argc>2) N_THREADS=atoi(argv[2]);
    if (argc>3) N_ITERS=atoi(argv[3]);
    if(!strcmp(mode,"shareddict")) MODE_DICT=1;
    else if(!strcmp(mode,"stats_nolock")){ MODE_STATS=1; MODE_LOCK=0; }
    else if(!strcmp(mode,"stats_lock")){ MODE_STATS=1; MODE_LOCK=1; }

    /* representative payload: semi-compressible text, plus a shared dictionary */
    SRCLEN = 8192; SRC = malloc(SRCLEN);
    for (unsigned i=0;i<SRCLEN;i++) SRC[i] = "the quick brown fox 0123456789 "[i % 31];
    static const unsigned char dict[] = "the quick brown fox jumps over the lazy dog 0123456789";
    DICT = dict; DICTLEN = sizeof(dict)-1;

    fprintf(stderr,"[zlib mode=%s threads=%d iters=%d]\n",mode,N_THREADS,N_ITERS);
    pthread_t th[256];
    for(long i=0;i<N_THREADS;i++) pthread_create(&th[i],0,worker,(void*)i);
    for(int i=0;i<N_THREADS;i++) pthread_join(th[i],0);
    fprintf(stderr,"[done in=%ld out=%ld count=%ld]\n", stats.in, stats.out, stats.count);
    return 0;
}

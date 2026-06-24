/*
 * Offload pattern taxonomy (TSan). The offloaded routine runs on the accelerator,
 * so cross-connection conflicts live entirely in the pre/post-processing shared
 * state. This harness enumerates the shared-state PATTERNS that real serving
 * classes use, and measures cross-connection conflicts for each. Application
 * classes are then mapped onto these patterns (see RESULTS).
 *
 * Each worker thread = an interleaved connection handler. Per event:
 *   preprocess (per-conn) -> offload stub (accelerator) -> postprocess (the pattern).
 *
 * Patterns:
 *   stateless     independent request, no shared writes        (img classify, per-obj crypto/compress)
 *   perconn       writes only this connection's own state      (TLS seqno, per-stream ctx)
 *   ro_shared     reads a big shared read-only blob            (model weights, shared dict)
 *   counter_lock  shared scalar aggregate, mutex
 *   counter_nolock shared scalar aggregate, NO lock            (naive metrics)
 *   queue_lock    shared ring buffer push/pop, mutex           (dynamic batching queue, locked)
 *   queue_nolock  shared ring buffer, NO lock                  (batching queue under run-to-completion)
 *   cache_lock    shared LRU-ish cache w/ eviction, mutex      (session cache, KV-cache allocator)
 *   cache_nolock  shared cache, NO lock                        (cache assuming single-thread RTC)
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int N_THREADS=16, N_ITERS=20000, PATTERN=0;

/* ---- shared structures under test ---- */
static volatile long g_counter;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
#define ROBLOB (1<<20)
static uint8_t *ro_blob;                 /* read-only "model weights" */
#define QN 4096
static long q_buf[QN]; static int q_head, q_tail;     /* shared ring (batch queue) */
#define CACHEN 256
struct ent { long key, val; };
static struct ent cache[CACHEN];         /* shared cache w/ eviction */
struct percon { long acc; char pad[64]; };
static struct percon *percon;            /* per-connection state array */

/* the accelerator offload: opaque heavy work, no shared state (runs on device) */
static long offload_stub(long x){ for(int i=0;i<32;i++) x=x*1103515245+12345; return x; }

static void postprocess(int id, int it, long r){
    /* shared "hot key": different connections request overlapping popular keys,
       as in a real result cache / KV-cache, so handlers touch the same entry. */
    int b = (it % 1000) & (CACHEN-1);
    switch(PATTERN){
    case 0: /* stateless */ break;
    case 1: /* perconn   */ percon[id].acc += r; break;
    case 2: /* ro_shared */ { long s=0; for(int i=0;i<256;i++) s+=ro_blob[(r+i)&(ROBLOB-1)]; percon[id].acc+=s; } break;
    case 3: /* counter_lock   */ pthread_mutex_lock(&g_mtx); g_counter+=r; pthread_mutex_unlock(&g_mtx); break;
    case 4: /* counter_nolock */ g_counter+=r; break;
    case 5: /* queue_lock   */ pthread_mutex_lock(&g_mtx); q_buf[q_head%QN]=r; q_head++; if(q_head-q_tail>QN)q_tail++; pthread_mutex_unlock(&g_mtx); break;
    case 6: /* queue_nolock */ q_buf[q_head%QN]=r; q_head++; if(q_head-q_tail>QN)q_tail++; break;
    case 7: /* cache_lock   */ pthread_mutex_lock(&g_mtx); if(cache[b].key==r) percon[id].acc+=cache[b].val; cache[b].key=r; cache[b].val=r^id; pthread_mutex_unlock(&g_mtx); break;
    case 8: /* cache_nolock */ if(cache[b].key==r) percon[id].acc+=cache[b].val; cache[b].key=r; cache[b].val=r^id; break;
    }
}

static void *worker(void *arg){
    int id=(int)(long)arg;
    for(int i=0;i<N_ITERS;i++){
        long x = ((long)id<<20) ^ i;       /* preprocess: per-conn input */
        long r = offload_stub(x);          /* offload (accelerator) */
        postprocess(id, i, r);             /* the pattern under test */
    }
    return 0;
}

static const char *NAMES[]={"stateless","perconn","ro_shared","counter_lock",
    "counter_nolock","queue_lock","queue_nolock","cache_lock","cache_nolock"};

int main(int argc,char**argv){
    const char *p = argc>1?argv[1]:"stateless";
    if(argc>2) N_THREADS=atoi(argv[2]);
    if(argc>3) N_ITERS=atoi(argv[3]);
    PATTERN=-1; for(int i=0;i<9;i++) if(!strcmp(p,NAMES[i])) PATTERN=i;
    if(PATTERN<0){ fprintf(stderr,"unknown pattern %s\n",p); return 2; }

    ro_blob=malloc(ROBLOB); for(int i=0;i<ROBLOB;i++) ro_blob[i]=i;
    percon=calloc(N_THREADS,sizeof *percon);

    fprintf(stderr,"[pattern=%s threads=%d iters=%d]\n",p,N_THREADS,N_ITERS);
    pthread_t th[256];
    for(long i=0;i<N_THREADS;i++) pthread_create(&th[i],0,worker,(void*)i);
    for(int i=0;i<N_THREADS;i++) pthread_join(th[i],0);
    long acc=0, csum=0;
    for(int i=0;i<N_THREADS;i++) acc+=percon[i].acc;
    for(int i=0;i<CACHEN;i++) csum+=cache[i].key^cache[i].val;   /* force cache stores live */
    fprintf(stderr,"[done counter=%ld q_head=%d acc=%ld csum=%ld]\n",g_counter,q_head,acc,csum);
    return 0;
}

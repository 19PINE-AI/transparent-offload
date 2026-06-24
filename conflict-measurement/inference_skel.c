/*
 * Phase 5: DNN/LLM inference-server shared-state hazard, modeled faithfully.
 *
 * Verified by source inspection: vLLM's scheduler + paged-KV-cache block allocator use
 * ZERO threading locks — they rely on the single-threaded engine step loop (run-to-completion).
 * This skeleton models the two hot shared structures and shows the CONSEQUENCE of interleaving
 * request handlers at the GPU-offload point (what transparent coroutines would introduce):
 *
 *   - paged KV-cache block allocator (free-list)   -> double-allocation if unlocked
 *   - dynamic batch queue (enqueue/dequeue)        -> lost/duplicated entries if unlocked
 *
 * Handlers (TSan thread model = the interleaving): grab KV blocks, "offload" (GPU forward),
 * free them, across a batch queue. unlocked (vLLM-style) vs locked.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

#define NBLOCK 2048
#define NREQ_BLOCKS 4
static int free_list[NBLOCK]; static int free_top;        /* KV-cache block allocator (stack) */
static int block_owner[NBLOCK];                            /* -1 free, else owning request id */
static long g_double_alloc=0, g_handled=0;
/* dynamic batch queue */
#define QN 4096
static long q_buf[QN]; static int q_head, q_tail;
static int LOCKED=0;
static pthread_mutex_t mtx=PTHREAD_MUTEX_INITIALIZER;
static int NTHREAD=8, ITERS=20000;

static inline void lk(){ if(LOCKED) pthread_mutex_lock(&mtx); }
static inline void ul(){ if(LOCKED) pthread_mutex_unlock(&mtx); }
static void brief_offload(void){ for(volatile int i=0;i<200;i++); }   /* GPU forward (stand-in) */

static void* handler(void* a){
    int id=(int)(long)a;
    int mine[NREQ_BLOCKS];
    for(int it=0; it<ITERS; it++){
        /* schedule: enqueue request into the shared batch queue */
        lk(); int slot=q_head%QN; q_buf[slot]=((long)id<<32)|it; q_head++; ul();
        /* allocate KV-cache blocks (prefill) */
        lk();
        for(int b=0;b<NREQ_BLOCKS;b++){
            if(free_top>0){ int blk=free_list[--free_top]; mine[b]=blk;
                if(block_owner[blk]!=-1) __sync_fetch_and_add(&g_double_alloc,1);  /* CORRUPTION: block handed out twice */
                block_owner[blk]=id;
            } else mine[b]=-1;
        }
        ul();
        /* offload: GPU forward pass (this is where a transparent coroutine would yield) */
        brief_offload();
        /* free KV blocks + dequeue (decode finished) */
        lk();
        for(int b=0;b<NREQ_BLOCKS;b++){ int blk=mine[b]; if(blk>=0){ block_owner[blk]=-1; free_list[free_top++]=blk; } }
        if(q_tail<q_head) q_tail++;
        ul();
        __sync_fetch_and_add(&g_handled,1);
    }
    return 0;
}

int main(int argc,char**argv){
    if(argc>1) LOCKED=!strcmp(argv[1],"locked");
    if(argc>2) NTHREAD=atoi(argv[2]);
    if(argc>3) ITERS=atoi(argv[3]);
    for(int i=0;i<NBLOCK;i++){ free_list[i]=i; block_owner[i]=-1; } free_top=NBLOCK;
    pthread_t th[64];
    for(long i=0;i<NTHREAD;i++) pthread_create(&th[i],0,handler,(void*)i);
    for(int i=0;i<NTHREAD;i++) pthread_join(th[i],0);
    /* integrity: every block must be free again and owned by nobody */
    int leaked=0, still_owned=0;
    for(int i=0;i<NBLOCK;i++) if(block_owner[i]!=-1) still_owned++;
    leaked = NBLOCK - free_top;
    printf("%s,threads=%d,handled=%ld,double_alloc=%ld,blocks_leaked=%d,blocks_still_owned=%d\n",
        LOCKED?"locked":"unlocked", NTHREAD, g_handled, g_double_alloc, leaked, still_owned);
    return 0;
}

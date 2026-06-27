/* detector_module.c — REAL end-to-end test of the page-protection conflict
 * detector on a stock Redis server, with a REAL GPU offload.
 *
 * Command:  DET.RMW <key>
 *   read shared counter for <key>  ->  GPU offload (libaccel_gpu_heavy: cuBLAS GEMM)
 *   ->  increment  ->  write back.   The read happens at command time and the write
 *   at reply time, so the read-modify-write SPANS the offload — exactly the overlap
 *   hazard. With many clients on the single event loop, two RMWs on the same key can
 *   interleave and lose updates.
 *
 * Modes (env RT_DET_MODE):
 *   naive   - full overlap, no protection enforced; the detector is ON and COUNTS the
 *             conflicts it sees (proves detection on a real server) -> lost updates > 0.
 *   detect  - detector ON + per-key serialization of the read->offload->write section,
 *             so conflicting (same-key) RMWs serialize while DIFFERENT keys stay
 *             overlapped -> 0 lost, throughput >> a coarse lock.
 *   lock    - coarse: serialize ALL offloads (one in flight) -> 0 lost, no overlap.
 *
 * Shared state: KSLOTS counters, ONE PER PAGE, in a dedicated mmap arena the detector
 * protects (per-key conflict granularity, no page aliasing). Stock Redis, module-only.
 *
 *   DET.STATS  -> [mode, total, sum, lost, conflicts, faults]
 *   DET.RESET  -> zero counters and stats
 */
#define REDISMODULE_EXPERIMENTAL_API
#include "redismodule.h"
#include "../../detector.h"
#include <pthread.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

static void (*accel_encrypt)(unsigned char*, int);
#define BUFSZ 4096
#define KSLOTS 256
#define PENDN  4096

enum { M_NAIVE=0, M_DETECT=1, M_LOCK=2 };
static int MODE = M_NAIVE;

static long  PGSZ;
static char *arena;                 /* KSLOTS pages, one counter per page */
static inline volatile long* cnt(int k){ return (volatile long*)(arena + (size_t)k*PGSZ); }

static volatile long g_total = 0;   /* ground-truth count of completed RMWs */

/* per-request job carried through the worker + reply */
typedef struct { RedisModuleBlockedClient *bc; int slot; long v; struct det_fib df; } job_t;

/* worker pool: runs the real GPU offload off the event loop */
#define QN 65536
static job_t* q[QN];
static volatile long qh=0, qt=0;
static pthread_mutex_t qmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  qcv = PTHREAD_COND_INITIALIZER;

/* per-gate serialization (gate = key for DETECT, gate 0 for LOCK). Main-thread only. */
static int    inflight[KSLOTS];
static job_t* pend[KSLOTS][PENDN];
static int    ph[KSLOTS], pt[KSLOTS];

static void enqueue_offload(job_t *j){
    pthread_mutex_lock(&qmx);
    q[qh % QN] = j; qh++;
    pthread_cond_signal(&qcv);
    pthread_mutex_unlock(&qmx);
}

/* read phase: snapshot the counter (and the detector version clock) then submit offload */
static void begin(job_t *j){
    j->v = *cnt(j->slot);                 /* READ (page is PROT_READ — no fault) */
    if (MODE != M_LOCK) det_park(&j->df);  /* snapshot version clock + reprotect (naive counts; detect enforces) */
    enqueue_offload(j);
}

static void* worker(void *a){
    (void)a; unsigned char buf[BUFSZ];
    for(;;){
        pthread_mutex_lock(&qmx);
        while(qh==qt) pthread_cond_wait(&qcv,&qmx);
        job_t *j = q[qt % QN]; qt++;
        pthread_mutex_unlock(&qmx);
        accel_encrypt(buf, BUFSZ);            /* REAL GPU offload (this worker only) */
        RedisModule_UnblockClient(j->bc, j);  /* wake event loop; carry job to reply */
    }
    return NULL;
}

static int reply_cb(RedisModuleCtx *ctx, RedisModuleString **argv, int argc){
    (void)argv;(void)argc;
    job_t *j = RedisModule_GetBlockedClientPrivateData(ctx);
    int k = j->slot;
    /* WRITE phase (main thread) */
    if (MODE != M_LOCK){ det_set_fiber(&j->df); j->df.active=1; det_reprotect(); }
    *cnt(k) = j->v + 1;                       /* RMW write (faults while detector active) */
    if (MODE != M_LOCK){ j->df.active=0; }
    __sync_fetch_and_add(&g_total, 1);
    /* advance the gate: start the next queued RMW on this gate, else mark idle */
    if (MODE != M_NAIVE){
        int g = (MODE==M_LOCK) ? 0 : k;
        if (ph[g] != pt[g]){ job_t *nx = pend[g][pt[g] % PENDN]; pt[g]++; begin(nx); }
        else inflight[g] = 0;
    }
    RedisModule_ReplyWithSimpleString(ctx, "OK");
    RedisModule_Free(j);
    return REDISMODULE_OK;
}
static int timeout_cb(RedisModuleCtx *ctx, RedisModuleString **a, int c){ (void)a;(void)c;
    return RedisModule_ReplyWithSimpleString(ctx, "TIMEOUT"); }

static unsigned slot_of(RedisModuleString *s){
    size_t n; const char *p = RedisModule_StringPtrLen(s, &n);
    unsigned h=2166136261u; for(size_t i=0;i<n;i++){ h^=(unsigned char)p[i]; h*=16777619u; }
    return h % KSLOTS;
}

static int cmd_rmw(RedisModuleCtx *ctx, RedisModuleString **argv, int argc){
    if (argc != 2) return RedisModule_WrongArity(ctx);
    int k = (int)slot_of(argv[1]);
    job_t *j = RedisModule_Alloc(sizeof(job_t));
    memset(j,0,sizeof *j); j->slot = k;
    j->bc = RedisModule_BlockClient(ctx, reply_cb, timeout_cb, NULL, 0);
    if (MODE == M_NAIVE){
        begin(j);                              /* always overlap */
    } else {
        int g = (MODE==M_LOCK) ? 0 : k;
        if (!inflight[g]){ inflight[g]=1; begin(j); }   /* gate free: go */
        else { pend[g][ph[g] % PENDN] = j; ph[g]++; }   /* gate busy: queue */
    }
    return REDISMODULE_OK;
}

static int cmd_stats(RedisModuleCtx *ctx, RedisModuleString **argv, int argc){
    (void)argv;(void)argc;
    long sum=0; for(int i=0;i<KSLOTS;i++) sum += *cnt(i);
    char b[256];
    /* flat, easy-to-parse: mode total sum lost conflicts faults */
    snprintf(b,sizeof b,"%s %ld %ld %ld %ld %ld",
        MODE==M_NAIVE?"naive":MODE==M_DETECT?"detect":"lock",
        g_total, sum, g_total - sum, det_conflicts, det_faults);
    return RedisModule_ReplyWithSimpleString(ctx, b);
}

static int cmd_reset(RedisModuleCtx *ctx, RedisModuleString **argv, int argc){
    (void)argv;(void)argc;
    if (det_on) mprotect(arena, (size_t)KSLOTS*PGSZ, PROT_READ|PROT_WRITE);
    for(int i=0;i<KSLOTS;i++){ *cnt(i)=0; inflight[i]=0; ph[i]=pt[i]=0; }
    g_total=0; det_conflicts=0; det_faults=0;
    if (det_on) mprotect(arena, (size_t)KSLOTS*PGSZ, PROT_READ);
    return RedisModule_ReplyWithSimpleString(ctx, "OK");
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc){
    (void)argv;(void)argc;
    if (RedisModule_Init(ctx,"det",1,REDISMODULE_APIVER_1)==REDISMODULE_ERR) return REDISMODULE_ERR;
    PGSZ = sysconf(_SC_PAGESIZE);
    char *m = getenv("RT_DET_MODE");
    if (m){ if(!strcmp(m,"detect")) MODE=M_DETECT; else if(!strcmp(m,"lock")) MODE=M_LOCK; else MODE=M_NAIVE; }

    char *lib = getenv("ACCEL_LIB"); if(!lib) lib="libaccel_gpu_heavy.so";
    void *h = dlopen(lib, RTLD_NOW|RTLD_GLOBAL); if(!h) return REDISMODULE_ERR;
    accel_encrypt = dlsym(h,"accel_encrypt"); if(!accel_encrypt) return REDISMODULE_ERR;

    arena = mmap(NULL,(size_t)KSLOTS*PGSZ,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (arena==MAP_FAILED) return REDISMODULE_ERR;
    memset(arena,0,(size_t)KSLOTS*PGSZ);
    if (MODE!=M_LOCK) det_init_region(arena,(size_t)KSLOTS*PGSZ);  /* detector on for naive(count) + detect(enforce) */

    int nw=16; char *e=getenv("ACCEL_WORKERS"); if(e) nw=atoi(e);
    for(int i=0;i<nw;i++){ pthread_t t; pthread_create(&t,NULL,worker,NULL); }

    if (RedisModule_CreateCommand(ctx,"det.rmw",  cmd_rmw,  "write",0,0,0)==REDISMODULE_ERR) return REDISMODULE_ERR;
    if (RedisModule_CreateCommand(ctx,"det.stats",cmd_stats,"readonly",0,0,0)==REDISMODULE_ERR) return REDISMODULE_ERR;
    if (RedisModule_CreateCommand(ctx,"det.reset",cmd_reset,"write",0,0,0)==REDISMODULE_ERR) return REDISMODULE_ERR;
    return REDISMODULE_OK;
}

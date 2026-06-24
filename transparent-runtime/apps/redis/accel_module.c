/* Redis loadable module: accelerator offload with overlap. ZERO edits to Redis source.
 * Two commands over the same offload (libaccel: accel_encrypt blocks for ACCEL_LAT_US):
 *   accel.sync   - offload runs ON the event-loop thread (blocks the whole server)
 *   accel.async  - offload runs on a worker pool; client is BlockClient'd and replied on
 *                  completion, so the single event loop keeps serving other clients (overlap)
 * The diff between the two is the "minimal edit" that unlocks the overlap.
 */
#define REDISMODULE_EXPERIMENTAL_API
#include "redismodule.h"
#include <pthread.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>

static void (*accel_encrypt)(unsigned char*, int);
#define BUFSZ 4096

/* ---- worker pool + job queue for the async path ---- */
#define QN 65536
typedef struct { RedisModuleBlockedClient *bc; } job_t;
static job_t q[QN];
static volatile long qhead=0, qtail=0;
static pthread_mutex_t qmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  qcv = PTHREAD_COND_INITIALIZER;

static void* worker(void *a) {
    (void)a;
    unsigned char buf[BUFSZ];
    for (;;) {
        pthread_mutex_lock(&qmx);
        while (qhead == qtail) pthread_cond_wait(&qcv, &qmx);
        job_t j = q[qtail % QN]; qtail++;
        pthread_mutex_unlock(&qmx);
        accel_encrypt(buf, BUFSZ);                  /* the offload (blocks this worker only) */
        RedisModule_UnblockClient(j.bc, NULL);      /* wake the event loop to reply */
    }
    return NULL;
}

static int reply_cb(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    (void)argv; (void)argc;
    return RedisModule_ReplyWithSimpleString(ctx, "OK");
}
static int timeout_cb(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    (void)argv; (void)argc;
    return RedisModule_ReplyWithSimpleString(ctx, "TIMEOUT");
}

/* accel.sync: offload on the event-loop thread — stalls the whole single-threaded server */
static int cmd_sync(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    (void)argv; (void)argc;
    unsigned char buf[BUFSZ];
    accel_encrypt(buf, BUFSZ);
    return RedisModule_ReplyWithSimpleString(ctx, "OK");
}

/* accel.async: hand the offload to the worker pool; event loop stays free (overlap) */
static int cmd_async(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    (void)argv; (void)argc;
    RedisModuleBlockedClient *bc =
        RedisModule_BlockClient(ctx, reply_cb, timeout_cb, NULL, 0);
    pthread_mutex_lock(&qmx);
    q[qhead % QN].bc = bc; qhead++;
    pthread_cond_signal(&qcv);
    pthread_mutex_unlock(&qmx);
    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    (void)argv; (void)argc;
    if (RedisModule_Init(ctx, "accel", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    char *lib = getenv("ACCEL_LIB"); if (!lib) lib = "libaccel.so";
    void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) return REDISMODULE_ERR;
    accel_encrypt = dlsym(h, "accel_encrypt");
    if (!accel_encrypt) return REDISMODULE_ERR;
    int nw = 16; char *e = getenv("ACCEL_WORKERS"); if (e) nw = atoi(e);
    for (int i = 0; i < nw; i++) { pthread_t t; pthread_create(&t, NULL, worker, NULL); }
    if (RedisModule_CreateCommand(ctx, "accel.sync",  cmd_sync,  "readonly", 0,0,0) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (RedisModule_CreateCommand(ctx, "accel.async", cmd_async, "readonly", 0,0,0) == REDISMODULE_ERR) return REDISMODULE_ERR;
    return REDISMODULE_OK;
}

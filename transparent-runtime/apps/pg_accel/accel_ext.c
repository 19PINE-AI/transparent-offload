/* PostgreSQL extension: accelerator offload, serial vs pipelined-overlap, within one query.
 * Postgres is process-per-connection, so the overlap is across the OFFLOADS of a single query
 * (many rows / a batch), not across connections: accel_sync(n) issues n offloads serially;
 * accel_async(n) keeps all n in flight on the accelerator (overlap). 0 lines of Postgres core. */
#include "postgres.h"
#include "fmgr.h"
#include <dlfcn.h>
PG_MODULE_MAGIC;

static void (*a_enc)(unsigned char*,int);
static long (*a_submit)(unsigned char*,int);
static int  (*a_done)(long);
static void (*a_release)(long);

static void resolve(void) {
    if (a_enc) return;
    char *l = getenv("ACCEL_LIB");
    void *h = dlopen(l ? l : "libaccel.so", RTLD_NOW|RTLD_GLOBAL);
    if (!h) elog(ERROR, "accel: dlopen failed: %s", dlerror());
    *(void**)&a_enc    = dlsym(h, "accel_encrypt");
    *(void**)&a_submit = dlsym(h, "accel_submit");
    *(void**)&a_done   = dlsym(h, "accel_done");
    *(void**)&a_release= dlsym(h, "accel_release");
}

PG_FUNCTION_INFO_V1(accel_sync);
Datum accel_sync(PG_FUNCTION_ARGS) {
    int n = PG_GETARG_INT32(0); resolve();
    unsigned char buf[4096];
    for (int i = 0; i < n; i++) a_enc(buf, 4096);   /* serial: each blocks until done */
    PG_RETURN_INT32(n);
}

PG_FUNCTION_INFO_V1(accel_async);
Datum accel_async(PG_FUNCTION_ARGS) {
    int n = PG_GETARG_INT32(0); resolve();
    unsigned char *bufs = palloc(n * 4096);
    long *ids = palloc(n * sizeof(long));
    for (int i = 0; i < n; i++) ids[i] = a_submit(bufs + i*4096, 4096); /* all in flight */
    for (int i = 0; i < n; i++) { while (!a_done(ids[i])) ; a_release(ids[i]); } /* collect */
    PG_RETURN_INT32(n);
}

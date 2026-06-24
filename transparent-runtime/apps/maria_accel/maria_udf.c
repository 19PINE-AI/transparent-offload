/* MariaDB UDF: accelerator offload, serial vs pipelined-overlap, within one query.
 * Like Postgres (process/thread-per-connection): the overlap is across a query's offloads.
 * Loaded with CREATE FUNCTION ... SONAME — 0 lines of MariaDB core edited. */
#include <mysql.h>
#include <dlfcn.h>
#include <stdlib.h>

static void (*a_enc)(unsigned char*,int);
static long (*a_submit)(unsigned char*,int);
static int  (*a_done)(long);
static void (*a_release)(long);
static void resolve(void){ if(a_enc) return;
    char*l=getenv("ACCEL_LIB"); void*h=dlopen(l?l:"libaccel.so",RTLD_NOW|RTLD_GLOBAL);
    *(void**)&a_enc=dlsym(h,"accel_encrypt"); *(void**)&a_submit=dlsym(h,"accel_submit");
    *(void**)&a_done=dlsym(h,"accel_done"); *(void**)&a_release=dlsym(h,"accel_release"); }

my_bool accel_sync_init(UDF_INIT *id, UDF_ARGS *a, char *msg){ (void)id;(void)a;(void)msg; return 0; }
long long accel_sync(UDF_INIT *id, UDF_ARGS *a, char *isnull, char *err){
    (void)id;(void)isnull;(void)err; resolve();
    long long n = *((long long*)a->args[0]);
    unsigned char buf[4096];
    for(long long i=0;i<n;i++) a_enc(buf,4096);        /* serial */
    return n;
}
my_bool accel_async_init(UDF_INIT *id, UDF_ARGS *a, char *msg){ (void)id;(void)a;(void)msg; return 0; }
long long accel_async(UDF_INIT *id, UDF_ARGS *a, char *isnull, char *err){
    (void)id;(void)isnull;(void)err; resolve();
    long long n = *((long long*)a->args[0]);
    unsigned char *bufs = malloc(n*4096); long *ids = malloc(n*sizeof(long));
    for(long long i=0;i<n;i++) ids[i]=a_submit(bufs+i*4096,4096);   /* all in flight */
    for(long long i=0;i<n;i++){ while(!a_done(ids[i])) ; a_release(ids[i]); }
    free(bufs); free(ids);
    return n;
}

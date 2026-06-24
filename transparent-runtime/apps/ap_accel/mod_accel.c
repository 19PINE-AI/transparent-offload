/* Apache module: accelerator offload handler. 0 lines of Apache core edited.
 * Apache is thread-pooled (mpm_worker/event): a sync offload blocks one worker thread, and the
 * worker POOL overlaps offloads across requests up to its size — the pool is the overlap engine. */
#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include <dlfcn.h>
#include <string.h>

static void (*accel_encrypt)(unsigned char*,int);
static void resolve(void){ if(accel_encrypt) return;
    char*l=getenv("ACCEL_LIB"); void*h=dlopen(l?l:"libaccel.so",RTLD_NOW|RTLD_GLOBAL);
    *(void**)&accel_encrypt=dlsym(h,"accel_encrypt"); }

static int accel_handler(request_rec *r){
    if (!r->handler || strcmp(r->handler, "accel")) return DECLINED;
    resolve();
    unsigned char buf[4096]; accel_encrypt(buf, 4096);   /* offload on this worker thread */
    ap_set_content_type(r, "text/plain");
    ap_rputs("OK", r);
    return OK;
}
static void register_hooks(apr_pool_t *p){ ap_hook_handler(accel_handler, NULL, NULL, APR_HOOK_MIDDLE); }
module AP_MODULE_DECLARE_DATA accel_module = {
    STANDARD20_MODULE_STUFF, NULL, NULL, NULL, NULL, NULL, register_hooks
};

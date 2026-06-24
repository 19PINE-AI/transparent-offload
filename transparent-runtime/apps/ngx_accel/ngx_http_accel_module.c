/* nginx module: accelerator offload, sync vs async-overlap. Idiomatic nginx thread-pool offload.
 *   location /sync  { accel_sync;  }   -> offload runs ON the worker's event loop (blocks it)
 *   location /async { accel_async; }   -> offload runs on nginx's thread pool; the event loop
 *                                         keeps serving other connections (overlap)
 * The offload itself is libaccel's accel_encrypt (blocks for ACCEL_LAT_US). dlopen'd at init.
 */
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_thread_pool.h>
#include <dlfcn.h>

#define BUFSZ 4096
static void (*accel_encrypt)(unsigned char *, int);

typedef struct { ngx_thread_pool_t *pool; } accel_loc_conf_t;
typedef struct { unsigned char buf[BUFSZ]; ngx_http_request_t *r; } accel_task_ctx_t;

static char *accel_sync(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *accel_async(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void *accel_create_loc_conf(ngx_conf_t *cf);

static ngx_command_t accel_commands[] = {
    { ngx_string("accel_sync"),  NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS, accel_sync,  NGX_HTTP_LOC_CONF_OFFSET, 0, NULL },
    { ngx_string("accel_async"), NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS, accel_async, NGX_HTTP_LOC_CONF_OFFSET, 0, NULL },
    ngx_null_command
};

static ngx_http_module_t accel_module_ctx = {
    NULL, NULL, NULL, NULL, NULL, NULL, accel_create_loc_conf, NULL
};
ngx_module_t ngx_http_accel_module = {
    NGX_MODULE_V1, &accel_module_ctx, accel_commands, NGX_HTTP_MODULE,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NGX_MODULE_V1_PADDING
};

static void *accel_create_loc_conf(ngx_conf_t *cf) {
    return ngx_pcalloc(cf->pool, sizeof(accel_loc_conf_t));
}

/* send a 2-byte "OK" body */
static ngx_int_t accel_send_ok(ngx_http_request_t *r) {
    static u_char body[] = "OK";
    ngx_buf_t *b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    ngx_chain_t out = { b, NULL };
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = sizeof(body) - 1;
    ngx_http_send_header(r);
    b->pos = body; b->last = body + sizeof(body) - 1;
    b->memory = 1; b->last_buf = 1; b->last_in_chain = 1;
    return ngx_http_output_filter(r, &out);
}

/* ---- sync: offload on the event-loop thread ---- */
static ngx_int_t accel_sync_handler(ngx_http_request_t *r) {
    ngx_int_t rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) return rc;
    unsigned char buf[BUFSZ];
    accel_encrypt(buf, BUFSZ);                 /* blocks this nginx worker entirely */
    return accel_send_ok(r);
}

/* ---- async: offload on the thread pool, overlap on the event loop ---- */
static void accel_thread_func(void *data, ngx_log_t *log) {
    accel_task_ctx_t *ctx = data;
    accel_encrypt(ctx->buf, BUFSZ);            /* runs in a thread-pool thread */
}
static void accel_thread_completion(ngx_event_t *ev) {
    ngx_http_request_t *r = ev->data;
    ngx_connection_t *c = r->connection;
    r->main->blocked--;                        /* release the aio hold BEFORE finalizing */
    r->aio = 0;
    ngx_int_t rc = accel_send_ok(r);
    ngx_http_finalize_request(r, rc);
    ngx_http_run_posted_requests(c);           /* required after async resume */
}
static ngx_int_t accel_async_handler(ngx_http_request_t *r) {
    ngx_int_t rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) return rc;
    accel_loc_conf_t *alcf = ngx_http_get_module_loc_conf(r, ngx_http_accel_module);
    ngx_thread_task_t *task = ngx_thread_task_alloc(r->pool, sizeof(accel_task_ctx_t));
    if (task == NULL) return NGX_HTTP_INTERNAL_SERVER_ERROR;
    accel_task_ctx_t *ctx = task->ctx;
    ctx->r = r;
    task->handler = accel_thread_func;
    task->event.handler = accel_thread_completion;
    task->event.data = r;
    task->event.log = r->connection->log;      /* event needs a log */
    if (ngx_thread_task_post(alcf->pool, task) != NGX_OK)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    r->main->blocked++;
    r->aio = 1;
    r->count++;                                 /* hold the request across the async gap */
    return NGX_DONE;                            /* reply on completion */
}

static char *accel_sync(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_core_loc_conf_t *clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = accel_sync_handler;
    if (!accel_encrypt) { char *_l=getenv("ACCEL_LIB"); void *h = dlopen(_l?_l:"libaccel.so", RTLD_NOW|RTLD_GLOBAL); if (h) accel_encrypt = dlsym(h, "accel_encrypt"); }
    return NGX_CONF_OK;
}
static char *accel_async(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    accel_loc_conf_t *alcf = conf;
    ngx_http_core_loc_conf_t *clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = accel_async_handler;
    ngx_str_t name = ngx_string("default");
    alcf->pool = ngx_thread_pool_add(cf, &name);
    if (alcf->pool == NULL) return "could not get thread pool 'default'";
    if (!accel_encrypt) { char *_l=getenv("ACCEL_LIB"); void *h = dlopen(_l?_l:"libaccel.so", RTLD_NOW|RTLD_GLOBAL); if (h) accel_encrypt = dlsym(h, "accel_encrypt"); }
    return NGX_CONF_OK;
}

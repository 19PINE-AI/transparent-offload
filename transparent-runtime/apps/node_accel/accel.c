#include <node_api.h>
#include <dlfcn.h>
#include <stdlib.h>
static void (*accel_encrypt)(unsigned char*,int);
static void resolve(void){ if(accel_encrypt) return;
  char*l=getenv("ACCEL_LIB"); void*h=dlopen(l?l:"libaccel.so",RTLD_NOW|RTLD_GLOBAL);
  *(void**)&accel_encrypt=dlsym(h,"accel_encrypt"); }

/* sync: offload on the JS event-loop thread (blocks the whole server) */
static napi_value AccelSync(napi_env env, napi_callback_info info){
  resolve(); unsigned char buf[4096]; accel_encrypt(buf,4096);
  napi_value r; napi_create_string_utf8(env,"OK",2,&r); return r;
}
/* async: offload on the libuv thread pool; event loop stays free (overlap) */
typedef struct { napi_async_work work; napi_ref cb; unsigned char buf[4096]; } Ctx;
static void Execute(napi_env env, void* d){ (void)env; Ctx*c=d; accel_encrypt(c->buf,4096); }
static void Complete(napi_env env, napi_status st, void* d){ (void)st; Ctx*c=d;
  napi_value cb,undef,arg,res; napi_get_reference_value(env,c->cb,&cb); napi_get_undefined(env,&undef);
  napi_create_string_utf8(env,"OK",2,&arg); napi_call_function(env,undef,cb,1,&arg,&res);
  napi_delete_reference(env,c->cb); napi_delete_async_work(env,c->work); free(c); }
static napi_value AccelAsync(napi_env env, napi_callback_info info){
  resolve(); size_t argc=1; napi_value argv[1]; napi_get_cb_info(env,info,&argc,argv,NULL,NULL);
  Ctx*c=calloc(1,sizeof(Ctx)); napi_create_reference(env,argv[0],1,&c->cb);
  napi_value name; napi_create_string_utf8(env,"accel",5,&name);
  napi_create_async_work(env,NULL,name,Execute,Complete,c,&c->work);
  napi_queue_async_work(env,c->work);
  napi_value u; napi_get_undefined(env,&u); return u;
}
static napi_value Init(napi_env env, napi_value ex){
  napi_value f; napi_create_function(env,NULL,0,AccelSync,NULL,&f); napi_set_named_property(env,ex,"accelSync",f);
  napi_create_function(env,NULL,0,AccelAsync,NULL,&f); napi_set_named_property(env,ex,"accelAsync",f);
  return ex;
}
NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)

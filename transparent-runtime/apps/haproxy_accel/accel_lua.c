#include <lua5.3/lua.h>
#include <lua5.3/lauxlib.h>
#include <dlfcn.h>
#include <stdlib.h>
static void (*ae)(unsigned char*,int);
static int l_offload(lua_State *L){
  if(!ae){ char*l=getenv("ACCEL_LIB"); void*h=dlopen(l?l:"libaccel.so",RTLD_NOW|RTLD_GLOBAL); *(void**)&ae=dlsym(h,"accel_encrypt"); }
  unsigned char b[4096]; ae(b,4096); lua_pushstring(L,"OK"); return 1;
}
int luaopen_accelmod(lua_State *L){ lua_newtable(L); lua_pushcfunction(L,l_offload); lua_setfield(L,-2,"offload"); return 1; }

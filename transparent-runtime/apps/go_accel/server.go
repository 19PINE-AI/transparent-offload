package main

/*
#cgo LDFLAGS: -ldl
#include <dlfcn.h>
#include <stdlib.h>
static void (*ae)(unsigned char*, int);
static void resolve(void){ if(ae) return; char*l=getenv("ACCEL_LIB");
    void*h=dlopen(l?l:"libaccel.so", RTLD_NOW|RTLD_GLOBAL); *(void**)&ae=dlsym(h,"accel_encrypt"); }
static void offload(void){ unsigned char buf[4096]; resolve(); ae(buf,4096); }
*/
import "C"
import (
    "fmt"
    "net/http"
)

// Plain blocking handler. No async code: Go's runtime runs each request in a goroutine and
// schedules other goroutines onto other OS threads while this one is blocked in the cgo offload.
func handler(w http.ResponseWriter, r *http.Request) {
    C.offload()
    fmt.Fprint(w, "OK")
}

func main() {
    http.HandleFunc("/", handler)
    http.ListenAndServe("127.0.0.1:7795", nil)
}

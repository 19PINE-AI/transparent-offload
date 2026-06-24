#include "accel.h"
/* baseline "CPU" implementation of the offloadable routine (a keyed transform). */
void accel_encrypt(unsigned char* buf, int n){
    unsigned k=0x9e3779b9u;
    for(int i=0;i<n;i++){ k = k*1664525u + 1013904223u; buf[i] ^= (unsigned char)(k>>17); }
}

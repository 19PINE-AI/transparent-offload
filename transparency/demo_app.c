#include <stdio.h>
#include <string.h>
#include "accel.h"
int main(void){
    enum{ NCONN=64, LEN=1024 };
    static unsigned char buf[NCONN][LEN];
    for(int c=0;c<NCONN;c++) memset(buf[c], (unsigned char)(c+1), LEN);   /* preprocess */
    for(int c=0;c<NCONN;c++) accel_encrypt(buf[c], LEN);                  /* OFFLOAD call */
    unsigned long sum=0;                                                  /* postprocess */
    for(int c=0;c<NCONN;c++) for(int i=0;i<LEN;i++) sum += buf[c][i];
    printf("APP checksum = %lu\n", sum);
    return 0;
}

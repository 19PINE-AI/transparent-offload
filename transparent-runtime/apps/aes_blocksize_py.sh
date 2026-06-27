#!/usr/bin/env bash
# AES block-size weight curve on Python asyncio (32-thread executor, not pool-capped
# like Node's libuv-4). sync vs async over real GPU AES block sizes.
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
LIB=$ROOT/libaccel_gpu.so
PYSITE=$(python3 -c 'import site;print(site.getusersitepackages())')
OUT=$ROOT/apps/aes_blocksize_py_results.csv
SIZES="${SIZES:-4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152 4194304 8388608}"
rps(){ taskset -c 4 ab -k -c 50 -t 5 -n 100000000 "$1" 2>/dev/null | awk '/Requests per second/{print $4}'; }
wp(){ for i in $(seq 1 80); do (echo >"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.2; done; return 1; }
cat > /tmp/aeslat.c <<'EOF'
#include <stdio.h>
#include <time.h>
extern void accel_encrypt(unsigned char*,int);
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
int main(){unsigned char b[4096]={0};accel_encrypt(b,4096);int R=150;double t0=now();
for(int i=0;i<R;i++)accel_encrypt(b,4096);printf("%.1f\n",(now()-t0)/R);return 0;}
EOF
gcc -O2 -o /tmp/aeslat /tmp/aeslat.c -L"$ROOT" -laccel_gpu -Wl,-rpath,"$ROOT" 2>/dev/null
echo "block_bytes,gpu_lat_us,sync_rps,async_rps,speedup" > "$OUT"
for SZ in $SIZES; do
  lat=$(ACCEL_AES_BYTES=$SZ /tmp/aeslat 2>/dev/null)
  pkill -f py_accel/server.py 2>/dev/null; sleep 0.5
  ACCEL_LIB=$LIB ACCEL_AES_BYTES=$SZ PYTHONPATH=$PYSITE taskset -c 2 python3 "$ROOT/apps/py_accel/server.py" </dev/null >/tmp/py_$SZ.log 2>&1 &
  if wp 7790; then s=$(rps http://127.0.0.1:7790/sync); a=$(rps http://127.0.0.1:7790/async)
    awk -v z="$SZ" -v L="$lat" -v s="$s" -v a="$a" 'BEGIN{printf "%d,%s,%s,%s,%.2f\n",z,L,s,a,(s+0>0)?a/s:0}' >> "$OUT"; fi
  pkill -f py_accel/server.py 2>/dev/null; sleep 0.4
done
touch "$ROOT/apps/.py_blocksize_done"

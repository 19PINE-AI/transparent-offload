#!/usr/bin/env bash
# Real-GPU AES block-size sweep on a single-event-loop server (Node.js): vary the
# AES block from 4 KiB to 1 MiB (ACCEL_AES_BYTES) and measure sync (offload on the
# loop) vs async (overlap) throughput. Block size = offload weight; this is the
# real-GPU AES weight curve. Single-op GPU latency per size is also recorded.
# Output: aes_blocksize_results.csv  (block_bytes,gpu_lat_us,sync_rps,async_rps,speedup)
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
LIB=$ROOT/libaccel_gpu.so
OUT=$ROOT/apps/aes_blocksize_results.csv
SIZES="${SIZES:-4096 16384 65536 262144 524288 1048576}"
rps(){ taskset -c 4 ab -k -c 50 -t 5 -n 100000000 "$1" 2>/dev/null | awk '/Requests per second/{print $4}'; }
wait_port(){ for i in $(seq 1 80); do (echo >"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.2; done; return 1; }

# tiny single-op latency probe (1 in flight), built once
cat > /tmp/aeslat.c <<'EOF'
#include <stdio.h>
#include <time.h>
extern void accel_encrypt(unsigned char*,int);
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
int main(){unsigned char b[4096]={0};accel_encrypt(b,4096);int R=200;double t0=now();
for(int i=0;i<R;i++)accel_encrypt(b,4096);printf("%.1f\n",(now()-t0)/R);return 0;}
EOF
gcc -O2 -o /tmp/aeslat /tmp/aeslat.c -L"$ROOT" -laccel_gpu -Wl,-rpath,"$ROOT" 2>/dev/null

echo "block_bytes,gpu_lat_us,sync_rps,async_rps,speedup" > "$OUT"
for SZ in $SIZES; do
  lat=$(ACCEL_AES_BYTES=$SZ /tmp/aeslat 2>/dev/null)
  pkill -f node_accel/server.js 2>/dev/null; sleep 0.5
  ACCEL_LIB=$LIB ACCEL_AES_BYTES=$SZ taskset -c 2 node "$ROOT/apps/node_accel/server.js" </dev/null >/tmp/node_$SZ.log 2>&1 &
  if wait_port 7780; then
    s=$(rps http://127.0.0.1:7780/sync); a=$(rps http://127.0.0.1:7780/async)
    awk -v z="$SZ" -v L="$lat" -v s="$s" -v a="$a" 'BEGIN{printf "%d,%s,%s,%s,%.2f\n",z,L,s,a,(s+0>0)?a/s:0}' | tee -a "$OUT"
  fi
  pkill -f node_accel/server.js 2>/dev/null; sleep 0.4
done
echo "=== aes_blocksize_results.csv ==="; cat "$OUT"

#!/usr/bin/env bash
# Real-GPU AES (launch-bound, libaccel_gpu.so) speedup for the app classes whose
# numbers were previously modeled: Node.js (single event loop) and Go (goroutine
# pool). No emulated latency. Output: app_gpu_results.csv
#   nodejs: /sync (blocks the loop) vs /async (libuv pool overlap), c=50
#   go    : c=1 serial vs c=64 (the goroutine runtime overlaps the offload)
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
LIB=$ROOT/libaccel_gpu.so
OUT=$ROOT/apps/app_gpu_results.csv
rps(){ taskset -c 4 ab -k -c "$2" -t 5 -n 100000000 "$1" 2>/dev/null \
       | grep -oE '[0-9.]+ requests per second' | head -1 | awk '{print $1}'; }
wait_port(){ for i in $(seq 1 80); do (echo >"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.2; done; return 1; }

echo "app,class,offload,baseline_rps,overlap_rps,speedup" > "$OUT"

# ---- Node.js (single event loop) ----
pkill -f node_accel/server.js 2>/dev/null; sleep 0.5
ACCEL_LIB=$LIB taskset -c 2 node "$ROOT/apps/node_accel/server.js" </dev/null >/tmp/node.log 2>&1 &
if wait_port 7780; then
  s=$(rps http://127.0.0.1:7780/sync 50); a=$(rps http://127.0.0.1:7780/async 50)
  awk -v s="$s" -v a="$a" 'BEGIN{printf "nodejs,single-event-loop,real-GPU-AES,%s,%s,%.2f\n",s,a,(s+0>0)?a/s:0}' | tee -a "$OUT"
fi
pkill -f node_accel/server.js 2>/dev/null; sleep 0.5

# ---- Go (goroutine pool) ----
ACCEL_LIB=$LIB taskset -c 2-9 "$ROOT/apps/go_accel/go_accel" </dev/null >/tmp/go.log 2>&1 &
if wait_port 7795; then
  s=$(rps http://127.0.0.1:7795/ 1); a=$(rps http://127.0.0.1:7795/ 64)
  awk -v s="$s" -v a="$a" 'BEGIN{printf "go,goroutine-pool,real-GPU-AES,%s,%s,%.2f\n",s,a,(s+0>0)?a/s:0}' | tee -a "$OUT"
fi
pkill -f go_accel/go_accel 2>/dev/null
echo "=== app_gpu_results.csv ==="; cat "$OUT"

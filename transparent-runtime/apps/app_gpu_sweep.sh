#!/usr/bin/env bash
# Real-GPU AES on 1 MiB blocks (realistic bulk crypto; ACCEL_AES_BYTES) for the app
# classes that were modeled before: Node.js (event loop), Go (goroutine pool),
# Apache (thread pool), HAProxy (proxy). Same AES offload everywhere. No emulation.
# Output: app_gpu_results.csv
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
LIB=$ROOT/libaccel_gpu.so
export ACCEL_AES_BYTES=${ACCEL_AES_BYTES:-1048576}     # 1 MiB AES blocks
PYSITE=$(python3 -c 'import site;print(site.getusersitepackages())')
HAP=$(command -v haproxy || echo /usr/sbin/haproxy)
APA=$(command -v apache2 || echo /usr/sbin/apache2)
OUT=$ROOT/apps/app_gpu_results.csv
rps(){ taskset -c 4 ab -k -c "$2" -t 5 -n 100000000 "$1" 2>/dev/null | awk '/Requests per second/{print $4}'; }
wait_port(){ for i in $(seq 1 80); do (echo >"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.2; done; return 1; }
echo "app,class,offload,baseline_rps,overlap_rps,speedup" > "$OUT"

# ---- Node.js (single event loop): /sync vs /async, c=50 ----
pkill -f node_accel/server.js 2>/dev/null; sleep 0.5
ACCEL_LIB=$LIB taskset -c 2 node "$ROOT/apps/node_accel/server.js" </dev/null >/tmp/node.log 2>&1 &
if wait_port 7780; then
  s=$(rps http://127.0.0.1:7780/sync 50); a=$(rps http://127.0.0.1:7780/async 50)
  awk -v s="$s" -v a="$a" 'BEGIN{printf "nodejs,single-event-loop,real-GPU-AES-1MB,%s,%s,%.2f\n",s,a,(s+0>0)?a/s:0}' | tee -a "$OUT"
fi
pkill -f node_accel/server.js 2>/dev/null; sleep 0.5

# ---- Go (goroutine pool): c=1 serial vs c=64 ----
ACCEL_LIB=$LIB taskset -c 2-9 "$ROOT/apps/go_accel/go_accel" </dev/null >/tmp/go.log 2>&1 &
if wait_port 7795; then
  s=$(rps http://127.0.0.1:7795/ 1); a=$(rps http://127.0.0.1:7795/ 64)
  awk -v s="$s" -v a="$a" 'BEGIN{printf "go,goroutine-pool,real-GPU-AES-1MB,%s,%s,%.2f\n",s,a,(s+0>0)?a/s:0}' | tee -a "$OUT"
fi
pkill -f go_accel/go_accel 2>/dev/null; sleep 0.5

# ---- Apache (mpm_worker thread pool, port 7811): c=1 vs c=64 ----
pkill -f "apache2.*ap_accel" 2>/dev/null; sleep 0.6
ACCEL_LIB=$LIB taskset -c 2-9 "$APA" -d "$ROOT/apps/ap_accel/run" \
  -f "$ROOT/apps/ap_accel/run/httpd.conf" -DFOREGROUND </dev/null >/tmp/apache.log 2>&1 &
if wait_port 7811; then
  s=$(rps http://127.0.0.1:7811/accel 1); a=$(rps http://127.0.0.1:7811/accel 64)
  awk -v s="$s" -v a="$a" 'BEGIN{printf "apache,thread-pool,real-GPU-AES-1MB,%s,%s,%.2f\n",s,a,(s+0>0)?a/s:0}' | tee -a "$OUT"
fi
pkill -f "apache2.*ap_accel" 2>/dev/null; sleep 0.6

# ---- HAProxy (proxy, SPOE agent): serial(1) vs overlapping(64) workers ----
HCFG=$ROOT/apps/haproxy_accel/haproxy_spoe.cfg
hrun(){
  pkill -f spoa_agent.py 2>/dev/null; pkill -f "haproxy_spoe.cfg" 2>/dev/null; sleep 0.7
  ACCEL_LIB=$LIB ACCEL_AES_BYTES=$ACCEL_AES_BYTES SPOA_WORKERS=$1 PYTHONPATH=$PYSITE \
    taskset -c 2-5 python3 "$ROOT/apps/haproxy_accel/spoa_agent.py" </dev/null >/tmp/spoa_$1.log 2>&1 &
  wait_port 9002 || { echo ""; return; }
  taskset -c 6-9 "$HAP" -f "$HCFG" </dev/null >/tmp/haproxy_$1.log 2>&1 &
  wait_port 7810 || { echo ""; return; }
  rps http://127.0.0.1:7810/ 64
  pkill -f spoa_agent.py 2>/dev/null; pkill -f "haproxy_spoe.cfg" 2>/dev/null; sleep 0.5
}
hs=$(hrun 1); ha=$(hrun 64)
awk -v s="$hs" -v a="$ha" 'BEGIN{printf "haproxy,proxy,real-GPU-AES-1MB,%s,%s,%.2f\n",s,a,(s+0>0)?a/s:0}' | tee -a "$OUT"

echo "=== app_gpu_results.csv ==="; cat "$OUT"

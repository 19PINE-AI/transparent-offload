#!/usr/bin/env bash
# Minimal: Redis / nginx / memcached at 1 MiB real-GPU AES. Short (DUR=4), records
# GPU util at each measurement so contended rows are visible. Output: rnm_results.csv
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
LIB=$ROOT/libaccel_gpu.so
export ACCEL_AES_BYTES=1048576
OUT=$ROOT/apps/rnm_results.csv
rps(){ taskset -c 4 ab -k -c 50 -t 4 -n 100000000 "$1" 2>/dev/null | awk '/Requests per second/{print $4}'; }
wp(){ for i in $(seq 1 60); do (echo >"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.2; done; return 1; }
util(){ nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -1; }
rm -f "$ROOT/apps/.rnm_done"
echo "app,class,sync_rps,async_rps,speedup,gpu_util" > "$OUT"

# Redis (single event loop)
pkill -f "redis-server.*7782" 2>/dev/null; sleep 0.5
ACCEL_LIB=$LIB redis-server --port 7782 --loadmodule "$ROOT/apps/redis/accel_module.so" --save '' --appendonly no --logfile /tmp/redis_g.log &
for i in $(seq 1 60); do [ "$(redis-cli -p 7782 ping 2>/dev/null)" = PONG ] && break; sleep 0.2; done
rb(){ redis-benchmark -q -p 7782 -n 20000 -c 50 "$1" 2>/dev/null | grep -oE '[0-9.]+ requests per second' | head -1 | awk '{print $1}'; }
rs=$(rb accel.sync); ra=$(rb accel.async); g=$(util)
awk -v s="$rs" -v a="$ra" -v g="$g" 'BEGIN{printf "redis,single-event-loop,%s,%s,%.2f,%s\n",s,a,(s+0>0)?a/s:0,g}' | tee -a "$OUT"
redis-cli -p 7782 shutdown nosave 2>/dev/null; sleep 0.4

# nginx (event loop + thread pool)
pkill -f "nginx-1.18.0/objs/nginx" 2>/dev/null; sleep 0.5
ACCEL_LIB=$LIB "$ROOT/apps/nginx-1.18.0/objs/nginx" -p "$ROOT/apps/nginx-run" -c "$ROOT/apps/nginx-run/nginx.conf" </dev/null >/tmp/nginx_g.log 2>&1 &
if wp 7750; then ns=$(rps http://127.0.0.1:7750/sync 50); na=$(rps http://127.0.0.1:7750/async 50); g=$(util)
  awk -v s="$ns" -v a="$na" -v g="$g" 'BEGIN{printf "nginx,event-loop-pool,%s,%s,%.2f,%s\n",s,a,(s+0>0)?a/s:0,g}' | tee -a "$OUT"; fi
"$ROOT/apps/nginx-1.18.0/objs/nginx" -p "$ROOT/apps/nginx-run" -s stop 2>/dev/null; sleep 0.5

# memcached (event loop + pool, -t 1)
pkill -f "memcached-1.6.18/memcached" 2>/dev/null; sleep 0.5
ACCEL_LIB=$LIB taskset -c 2 "$ROOT/apps/memcached-1.6.18/memcached" -p 7760 -t 1 -m 256 </dev/null >/tmp/mc.log 2>&1 &
if wp 7760; then ms=$(python3 "$ROOT/apps/mc_bench.py" 7760 accelsync 50 4); ma=$(python3 "$ROOT/apps/mc_bench.py" 7760 accelasync 50 4); g=$(util)
  awk -v s="$ms" -v a="$ma" -v g="$g" 'BEGIN{printf "memcached,event-loop-pool,%s,%s,%.2f,%s\n",s,a,(s+0>0)?a/s:0,g}' | tee -a "$OUT"; fi
pkill -f "memcached-1.6.18/memcached" 2>/dev/null
touch "$ROOT/apps/.rnm_done"
echo "=== rnm_results.csv ==="; cat "$OUT"

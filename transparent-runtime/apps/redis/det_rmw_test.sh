#!/usr/bin/env bash
# =====================================================================
# Detector on a REAL stateful server (stock Redis) with a REAL GPU offload.
# Drives DET.RMW (read -> GPU GEMM offload -> increment -> write) from many
# clients across a key space, in three modes, and reports lost updates,
# detected conflicts, and throughput:
#   naive  : full overlap, detector COUNTS conflicts (no enforce)  -> lost > 0
#   detect : detector + per-key serialize-on-conflict, keys overlap-> 0 lost
#   lock   : coarse (one offload in flight)                        -> 0 lost, no overlap
# Two contention levels (small/large key space). Needs a free GPU window.
#
# Output: det_rmw_results.csv  (mode,keyspace,total,sum,lost,conflicts,rps)
# =====================================================================
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
LIB=${LIB:-$ROOT/libaccel_gpu_heavy.so}
PORT=${PORT:-6790}
N=${N:-30000}; C=${C:-50}
GEMM_N=${GEMM_N:-1024}; ITERS=${ITERS:-1}; WORKERS=${WORKERS:-64}
OUT=$HERE/det_rmw_results.csv
echo "mode,keyspace,total,sum,lost,conflicts,rps" > "$OUT"

stop(){ redis-cli -p $PORT shutdown nosave 2>/dev/null; pkill -f "redis-server .*:$PORT" 2>/dev/null; sleep 0.5; }

run(){ local mode=$1 ks=$2
  stop
  RT_DET_MODE=$mode ACCEL_LIB=$LIB ACCEL_GEMM_N=$GEMM_N ACCEL_GEMM_ITERS=$ITERS ACCEL_WORKERS=$WORKERS \
    redis-server --port $PORT --loadmodule "$HERE/detector_module.so" \
    --save '' --appendonly no --logfile /tmp/det_redis_$mode.log &
  for i in $(seq 1 60); do [ "$(redis-cli -p $PORT ping 2>/dev/null)" = PONG ] && break; sleep 0.2; done
  redis-cli -p $PORT det.reset >/dev/null
  local rps
  rps=$(redis-benchmark -q -p $PORT -n $N -c $C -r $ks det.rmw "key:__rand_int__" 2>/dev/null \
        | grep -oE '[0-9.]+ requests per second' | head -1 | awk '{print $1}')
  # det.stats -> "mode total sum lost conflicts faults"
  local st total sum lost conf
  st=$(redis-cli -p $PORT det.stats)
  total=$(echo "$st" | awk '{print $2}'); sum=$(echo "$st" | awk '{print $3}')
  lost=$(echo "$st" | awk '{print $4}');  conf=$(echo "$st" | awk '{print $5}')
  echo "$mode,$ks,$total,$sum,$lost,$conf,$rps" | tee -a "$OUT"
  stop
}

for ks in ${KSPACES:-8 100000}; do
  for mode in naive detect lock; do run $mode "$ks"; done
done
echo "=== det_rmw_results.csv ==="; cat "$OUT"

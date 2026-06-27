#!/usr/bin/env bash
# =====================================================================
# REAL-GPU weight sweep (no emulated latency). The offload is a real
# cuBLAS SGEMM on the RTX 6000 (libaccel_gpu_heavy.so); its latency is
# set by the GEMM size, swept here. For each size we record sync vs
# async (overlap) throughput on the single-event-loop Python server.
# Map GEMM_N -> real GPU latency via gemm_calib.csv (./gemm_calib).
#
# Output: weight_sweep_gpu.csv  (server,gemm_n,iters,sync_rps,async_rps,speedup)
# Requires a free GPU window (each in-flight offload uses ~3*N^2*4 bytes).
# =====================================================================
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
LIB=$ROOT/libaccel_gpu_heavy.so
PYSITE=$(python3 -c 'import site;print(site.getusersitepackages())')
NS="${NS:-256 512 768 1024 1536 2048}"     # GEMM sizes (-> real latencies)
ITERS="${ITERS:-1}"
DUR=5; C=50
OUT=$ROOT/apps/weight_sweep_gpu.csv
echo "server,gemm_n,iters,sync_rps,async_rps,speedup" > "$OUT"

rps(){ taskset -c 4 ab -k -c $C -t $DUR -n 100000000 "$1" 2>/dev/null \
       | awk '/Requests per second/{print $4}'; }
wait_port(){ for i in $(seq 1 80); do (echo >"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.2; done; return 1; }
spd(){ awk -v a="$1" -v s="$2" 'BEGIN{printf "%.2f",(s+0>0)?a/s:0}'; }

for N in $NS; do
  pkill -f py_accel/server.py 2>/dev/null; sleep 0.5
  ACCEL_LIB=$LIB ACCEL_GEMM_N=$N ACCEL_GEMM_ITERS=$ITERS PYTHONPATH=$PYSITE taskset -c 2 \
       python3 "$ROOT/apps/py_accel/server.py" </dev/null >/dev/null 2>&1 &
  wait_port 7790 || { echo "python,$N,$ITERS,START_FAIL,,"; continue; }
  s=$(rps http://127.0.0.1:7790/sync); a=$(rps http://127.0.0.1:7790/async)
  echo "python,$N,$ITERS,$s,$a,$(spd "$a" "$s")" | tee -a "$OUT"
  pkill -f py_accel/server.py 2>/dev/null; sleep 0.4
done
echo "=== weight_sweep_gpu.csv ==="; cat "$OUT"

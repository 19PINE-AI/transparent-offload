#!/usr/bin/env bash
# =====================================================================
# Weight sweep: speedup of async/overlap vs synchronous offload as the
# offload latency (ACCEL_LAT_US) is swept, for the single-event-loop
# servers (Python asyncio, Node.js). Demonstrates the paper's claim that
# overlap pays only when the offload outweighs per-request CPU work:
# speedup ~1x for a light offload, rising as the offload grows.
#
# Core layout: device=6 (SCHED_FIFO via sudo), python=2, node=3, ab=4.
# Output: weight_sweep.csv  (server,lat_us,sync_rps,async_rps,speedup)
#
# Requirements: a host whose scheduler/cgroup will not kill the device's
# busy-poll core (use sudo for SCHED_FIFO, ideally an isolated core). The
# components are verified individually; a single point measured cleanly at
# ACCEL_LAT_US=1000 gave python sync=466 / async=6341 rps = 13.6x, matching
# the paper's 13.8x heavy-offload point. Run on a stable RT host to emit the
# full latency->speedup curve, then wire weight_sweep.csv into fig_weight.
# =====================================================================
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
LIB=$ROOT/libaccel.so
NODE=$(which node)
SUDO=${SUDO-sudo}    # set SUDO= to run without RT priority (device latency is busy-poll enforced either way)
PYSITE=$(python3 -c 'import site;print(site.getusersitepackages())')   # so root python finds aiohttp
LATS="5 10 20 40 80 160 320 640 1000"
DUR=5            # ab time limit per route (s)
C=50             # concurrency (matches the paper)
OUT=$ROOT/apps/weight_sweep.csv
echo "server,lat_us,sync_rps,async_rps,speedup" > "$OUT"

rps(){ taskset -c 4 ab -k -c $C -t $DUR -n 100000000 "$1" 2>/dev/null \
       | awk '/Requests per second/{print $4}'; }
wait_port(){ for i in $(seq 1 60); do (echo >"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.2; done; return 1; }
speedup(){ awk -v a="$1" -v s="$2" 'BEGIN{printf "%.2f",(s+0>0)?a/s:0}'; }

# ---- Python asyncio (port 7790: /sync, /async) ----
for L in $LATS; do
  $SUDO pkill -f py_accel/server.py 2>/dev/null; sleep 0.5
  setsid $SUDO env ACCEL_LIB=$LIB ACCEL_LAT_US=$L ACCEL_CORE=6 PYTHONPATH=$PYSITE taskset -c 2 \
       python3 "$ROOT/apps/py_accel/server.py" </dev/null >/dev/null 2>&1 &
  wait_port 7790 || { echo "python,$L,START_FAIL,,"; continue; }
  s=$(rps http://127.0.0.1:7790/sync); a=$(rps http://127.0.0.1:7790/async)
  echo "python,$L,$s,$a,$(speedup "$a" "$s")" | tee -a "$OUT"
  $SUDO pkill -f py_accel/server.py 2>/dev/null; sleep 0.4
done

# ---- Node.js (port 7780: /sync blocks, else async via libuv pool) ----
cd "$ROOT/apps/node_accel"
for L in $LATS; do
  $SUDO pkill -f node_accel/server.js 2>/dev/null; sleep 0.5
  setsid $SUDO env ACCEL_LIB=$LIB ACCEL_LAT_US=$L ACCEL_CORE=6 taskset -c 3 \
       "$NODE" "$ROOT/apps/node_accel/server.js" </dev/null >/dev/null 2>&1 &
  wait_port 7780 || { echo "node,$L,START_FAIL,,"; continue; }
  s=$(rps http://127.0.0.1:7780/sync); a=$(rps http://127.0.0.1:7780/async)
  echo "node,$L,$s,$a,$(speedup "$a" "$s")" | tee -a "$OUT"
  $SUDO pkill -f node_accel/server.js 2>/dev/null; sleep 0.4
done

echo "=== weight_sweep.csv ==="; cat "$OUT"

#!/usr/bin/env bash
# C SPOA agent (no GIL) vs HAProxy: serial(1 worker) vs overlapping(64) at 1MB AES.
# Validates the SPOP handshake (smoke http code) before measuring. Output: haproxy_c_results.csv
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
LIB=$ROOT/libaccel_gpu.so
export ACCEL_AES_BYTES=${ACCEL_AES_BYTES:-1048576}
AG=$ROOT/apps/haproxy_accel/spoa_agent_c
HCFG=$ROOT/apps/haproxy_accel/haproxy_spoe.cfg
HAP=$(command -v haproxy || echo /usr/sbin/haproxy)
OUT=$ROOT/apps/haproxy_c_results.csv
rps(){ taskset -c 4 ab -k -c 50 -t 5 -n 100000000 "$1" 2>/dev/null | awk '/Requests per second/{print $4}'; }
wp(){ for i in $(seq 1 60); do (echo >"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.2; done; return 1; }
util(){ nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -1; }
rm -f "$ROOT/apps/.haproxy_c_done"
echo "agent,workers,smoke_http,rps,gpu_util" > "$OUT"
run(){  # $1 = worker count
  pkill -f spoa_agent_c 2>/dev/null; pkill -f "haproxy_spoe.cfg" 2>/dev/null; sleep 0.8
  ACCEL_LIB=$LIB ACCEL_AES_BYTES=$ACCEL_AES_BYTES SPOA_WORKERS=$1 taskset -c 2-5 "$AG" 9002 </dev/null >/tmp/cagent_$1.log 2>&1 &
  wp 9002 || { echo "C,$1,AGENT_FAIL,,"; return; }
  taskset -c 6-9 "$HAP" -f "$HCFG" </dev/null >/tmp/chap_$1.log 2>&1 &
  wp 7810 || { echo "C,$1,HAP_FAIL,,"; return; }
  code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:7810/ 2>/dev/null)
  r=$(rps http://127.0.0.1:7810/); g=$(util)
  echo "C,$1,$code,$r,$g" | tee -a "$OUT"
  pkill -f spoa_agent_c 2>/dev/null; pkill -f "haproxy_spoe.cfg" 2>/dev/null; sleep 0.5
}
run 1
run 64
touch "$ROOT/apps/.haproxy_c_done"
echo "=== haproxy_c_results.csv ==="; cat "$OUT"

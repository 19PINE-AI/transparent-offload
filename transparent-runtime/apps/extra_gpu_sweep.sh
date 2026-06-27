#!/usr/bin/env bash
# Closes the one real-GPU gap: Apache (mpm_worker thread pool) with the SAME real
# GPU AES offload (4 KB, libaccel_gpu.so) every other app uses. c=1 serial vs c=64
# pool-overlap. No emulation. Output: extra_gpu_results.csv
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
AESLIB=$ROOT/libaccel_gpu.so
APA=$(command -v apache2 || echo /usr/sbin/apache2)
OUT=$ROOT/apps/extra_gpu_results.csv
rps(){ taskset -c 4 ab -k -c "$2" -t 5 -n 100000000 "$1" 2>/dev/null | awk '/Requests per second/{print $4}'; }
wait_port(){ for i in $(seq 1 80); do (echo >"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.2; done; return 1; }
echo "app,class,offload,baseline_rps,overlap_rps,speedup" > "$OUT"

# ---- Apache (mpm_worker thread pool): real GPU AES, c=1 serial vs c=64 pool-overlap ----
pkill -f "apache2.*ap_accel" 2>/dev/null; sleep 0.6
ACCEL_LIB=$AESLIB taskset -c 2-9 "$APA" -d "$ROOT/apps/ap_accel/run" \
  -f "$ROOT/apps/ap_accel/run/httpd.conf" -DFOREGROUND </dev/null >/tmp/apache.log 2>&1 &
if wait_port 7800; then
  s=$(rps http://127.0.0.1:7800/accel 1); a=$(rps http://127.0.0.1:7800/accel 64)
  awk -v s="$s" -v a="$a" 'BEGIN{printf "apache,thread-pool,real-GPU-AES,%s,%s,%.2f\n",s,a,(s+0>0)?a/s:0}' | tee -a "$OUT"
fi
pkill -f "apache2.*ap_accel" 2>/dev/null

echo "=== extra_gpu_results.csv ==="; cat "$OUT"

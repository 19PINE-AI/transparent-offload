#!/usr/bin/env bash
# Real REMOTE offload (no GPU, no emulation): a single-event-loop server (Python
# asyncio) offloads to remote_hsm — a real RSA-2048 signer over TCP. sync runs the
# RPC on the event loop (blocks the whole server); async runs it on the worker pool
# (overlap). The offload is latency-bound (real network round-trip + real crypto),
# so many RPCs are in flight -> the dramatic event-loop multiplier, honestly.
# Output: remote_hsm_results.csv
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
PYSITE=$(python3 -c 'import site;print(site.getusersitepackages())')
HPORT=${HPORT:-7900}
OUT=$ROOT/remote_hsm_results.csv
pkill -x remote_hsm 2>/dev/null; pkill -f py_accel/server.py 2>/dev/null; sleep 0.6

# real remote HSM on its own cores
taskset -c 8-23 "$ROOT/remote_hsm" $HPORT >/tmp/hsm.log 2>&1 &
HPID=$!
sleep 1.5

# single-RPC (1 in flight) real latency, for the record
cat > /tmp/lat.c <<'EOF'
#include <stdio.h>
#include <time.h>
extern void accel_encrypt(unsigned char*,int);
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
int main(){unsigned char b[64]={0};accel_encrypt(b,64);int R=1000;double t0=now();
for(int i=0;i<R;i++)accel_encrypt(b,64);printf("%.1f\n",(now()-t0)/R);return 0;}
EOF
gcc -O2 -o /tmp/lat /tmp/lat.c -L"$ROOT" -laccel_remote -Wl,-rpath,"$ROOT" 2>/dev/null
LAT=$(ACCEL_REMOTE_PORT=$HPORT /tmp/lat 2>/dev/null)

# Python single-event-loop server, offload = real remote HSM
ACCEL_LIB=$ROOT/libaccel_remote.so ACCEL_REMOTE_PORT=$HPORT PYTHONPATH=$PYSITE taskset -c 2 \
  python3 "$ROOT/apps/py_accel/server.py" >/tmp/pysrv.log 2>&1 &
for i in $(seq 1 80); do (echo >/dev/tcp/127.0.0.1/7790) 2>/dev/null && break; sleep 0.2; done

rps(){ taskset -c 4 ab -k -c 50 -t 6 -n 100000000 "$1" 2>/dev/null | awk '/Requests per second/{print $4}'; }
s=$(rps http://127.0.0.1:7790/sync)
a=$(rps http://127.0.0.1:7790/async)

echo "server,offload,rpc_lat_us,sync_rps,async_rps,speedup" > "$OUT"
awk -v L="$LAT" -v s="$s" -v a="$a" \
  'BEGIN{printf "python,real-remote-RSA2048,%s,%s,%s,%.2f\n",L,s,a,(s+0>0)?a/s:0}' | tee -a "$OUT"

pkill -f py_accel/server.py 2>/dev/null; kill $HPID 2>/dev/null; pkill -x remote_hsm 2>/dev/null
echo DONE

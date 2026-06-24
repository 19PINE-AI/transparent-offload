#!/bin/bash
# Additional sensitivity experiments + statistical rigor. Run after sweep_L_reps.
set -e
cd /home/ubuntu/transparent-offload/runtime

echo "=== [1/4] W-sweep: sensitivity to CPU work per request (L=20us, C=128, 6 reps) ==="
echo "rep,mode,L_us,C,W_us,tput_reqps,p50_us,p99_us,p999_us" > sweep_W_reps.csv
for rep in 1 2 3 4 5 6; do
  for W in 500 1000 2000 4000 8000 16000; do
    for m in busy block coro; do
      out=$(timeout 25 sudo ./accel_compare $m 20000 128 $W 1200)
      echo "$rep,$out" >> sweep_W_reps.csv
    done
  done
done
echo "W-sweep done: $(wc -l < sweep_W_reps.csv) lines"

echo "=== [2/4] block thread-count sensitivity (L=20us, W=2us, vary C threads, 6 reps) ==="
echo "rep,mode,L_us,C,W_us,tput_reqps,p50_us,p99_us,p999_us" > block_threads_reps.csv
for rep in 1 2 3 4 5 6; do
  for c in 2 4 8 16 32 64 128 256; do
    out=$(timeout 25 sudo ./accel_compare block 20000 $c 2000 1200)
    echo "$rep,$out" >> block_threads_reps.csv
  done
done
echo "block-threads done: $(wc -l < block_threads_reps.csv) lines"

echo "=== [3/4] conflict detector statistical reps (8 reps per contention level) ==="
echo "rep,mode,NKEY,C,commits,sum,lost_updates,fallbacks,fallback_pct,tput_ops_s" > detector_reps.csv
for rep in 1 2 3 4 5 6 7 8; do
  for k in 1 16 64 256 1024 16384; do
    for m in naive detect; do
      out=$(timeout 25 sudo ./conflict_detector $m $k 64 20000)
      echo "$rep,$out" >> detector_reps.csv
    done
  done
done
echo "detector-reps done: $(wc -l < detector_reps.csv) lines"

echo "=== [4/4] GPU runtime ratio reps (coro N=32 vs busy, 6 reps) — GPU may be contended ==="
echo "rep,mode,N,MSG,WCPU_us,ops,tput_ops_s,GBps" > gpu_reps.csv
for rep in 1 2 3 4 5 6; do
  for m in busy coro; do
    out=$(timeout 40 ./gpu_runtime $m 32 4096 3000 2>/dev/null | grep -v correctness)
    echo "$rep,$out" >> gpu_reps.csv
  done
done
echo "gpu-reps done: $(wc -l < gpu_reps.csv) lines"
echo "=== ALL MORE EXPERIMENTS DONE ==="

#!/usr/bin/env bash
# Wait for a free GPU window on this shared box, then run the real-GPU
# experiments (no emulated latency): GEMM latency calibration + the real-GPU
# weight sweep. Calibration is pure GPU (robust); the server sweep is best
# effort. Results: gemm_calib.csv, apps/weight_sweep_gpu.csv. Status: gpu_run.log
set -u
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
THRESH_MIB=${THRESH_MIB:-12000}      # need ~12 GiB free
INTERVAL=${INTERVAL:-60}
MAX_WAIT=${MAX_WAIT:-21600}          # give up after 6 h
LOG=$ROOT/gpu_run.log
start=$(date +%s)
echo "$(date '+%F %T') monitor start; need ${THRESH_MIB}MiB free" > "$LOG"
while :; do
  free=$(nvidia-smi --query-gpu=memory.free --format=csv,noheader,nounits 2>/dev/null | head -1)
  free=${free:-0}
  now=$(date +%s)
  if [ "$free" -ge "$THRESH_MIB" ]; then
    echo "$(date '+%F %T') GPU free=${free}MiB -> running experiments" >> "$LOG"
    cd "$ROOT"
    echo "== calibration ==" >> "$LOG"
    ./gemm_calib 1 >  gemm_calib.csv      2>>"$LOG"
    ./gemm_calib 4 >> gemm_calib_i4.csv   2>>"$LOG"
    echo "== real-GPU weight sweep ==" >> "$LOG"
    bash apps/weight_sweep_gpu.sh > apps/weight_sweep_gpu.log 2>&1
    echo "$(date '+%F %T') DONE" >> "$LOG"
    break
  fi
  if [ $((now - start)) -ge "$MAX_WAIT" ]; then
    echo "$(date '+%F %T') gave up after ${MAX_WAIT}s; GPU still busy (free=${free}MiB)" >> "$LOG"
    break
  fi
  sleep "$INTERVAL"
done

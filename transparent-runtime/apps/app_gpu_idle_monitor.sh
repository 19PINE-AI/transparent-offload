#!/usr/bin/env bash
# Wait for a genuinely IDLE GPU (low utilization AND free memory) on this shared
# box, then run the 1 MiB-AES app sweep cleanly. Clean GPU numbers are impossible
# while another tenant is computing (kernels queue), so we gate on util + free mem.
# Trivial loop body + detached attempt -> survives the box's SIGSTKFLT bursts.
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
DONE=$ROOT/apps/.gpu_all_done
LOG=$ROOT/apps/app_gpu_run.log
MAXUTIL=${MAXUTIL:-12}; MINFREE=${MINFREE:-4000}
echo "$(date '+%F %T') idle-GPU monitor start (util<${MAXUTIL}%, free>${MINFREE}MiB)" > "$LOG"
rm -f "$DONE"
for t in $(seq 1 240); do
  sleep 60
  if [ -f "$DONE" ]; then
    echo "$(date '+%F %T') SUCCESS window $t" >> "$LOG"
    cat "$ROOT/apps/aes_blocksize_results.csv" "$ROOT/apps/app_gpu_results.csv" >> "$LOG" 2>/dev/null; break
  fi
  read -r util free < <(nvidia-smi --query-gpu=utilization.gpu,memory.free --format=csv,noheader,nounits 2>/dev/null | head -1 | tr ',' ' ')
  util=${util:-100}; free=${free:-0}
  if [ "$util" -le "$MAXUTIL" ] && [ "$free" -ge "$MINFREE" ] && ! pgrep -f "aes_blocksize_sweep.sh|app_gpu_sweep.sh" >/dev/null 2>&1; then
    echo "$(date '+%F %T') GPU idle (util=${util}% free=${free}MiB) -> run attempt $t" >> "$LOG"
    setsid nohup bash "$ROOT/apps/run_all_gpu.sh" </dev/null >> "$LOG" 2>&1 &
  fi
done
echo "$(date '+%F %T') monitor done" >> "$LOG"

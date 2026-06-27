#!/usr/bin/env bash
# Opportunistic retry for the real-GPU app sweep (Node.js, Go). Trivial loop body
# + detached attempts + sleep-first, so it survives the box's SIGSTKFLT bursts and
# fires during a lull. Stops when app_gpu_results.csv has both data rows.
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
CSV=$ROOT/apps/app_gpu_results.csv
LOG=$ROOT/apps/app_gpu_run.log
echo "$(date '+%F %T') app-GPU retry start" > "$LOG"
for t in $(seq 1 200); do
  sleep 90
  if [ -s "$CSV" ] && [ "$(wc -l < "$CSV")" -ge 3 ]; then
    echo "$(date '+%F %T') SUCCESS at window $t" >> "$LOG"; cat "$CSV" >> "$LOG"; break
  fi
  if ! pgrep -f app_gpu_sweep.sh >/dev/null 2>&1; then
    echo "$(date '+%F %T') spawn attempt $t" >> "$LOG"
    setsid nohup bash "$ROOT/apps/app_gpu_sweep.sh" </dev/null >> "$LOG" 2>&1 &
  fi
done
echo "$(date '+%F %T') app-GPU retry done" >> "$LOG"

#!/usr/bin/env bash
# The box kills CPU-heavy foreground bursts (SIGSTKFLT) intermittently, but a
# mostly-sleeping detached loop survives and can run an attempt during a lull
# (this is how gpu_monitor caught its window). Each attempt runs in its own
# process group (setsid) so a burst-kill of the attempt doesn't take down the loop.
# Stops as soon as remote_hsm_results.csv has a data row.
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
CSV=$ROOT/remote_hsm_results.csv
LOG=$ROOT/remote_hsm_run.log
echo "$(date '+%F %T') remote-HSM retry loop start" > "$LOG"
rm -f "$CSV"
for t in $(seq 1 30); do
  echo "$(date '+%F %T') attempt $t" >> "$LOG"
  setsid bash "$ROOT/remote_hsm_sweep.sh" </dev/null >>"$LOG" 2>&1 || true
  if [ -s "$CSV" ] && [ "$(wc -l < "$CSV")" -ge 2 ]; then
    echo "$(date '+%F %T') SUCCESS on attempt $t" >> "$LOG"; cat "$CSV" >> "$LOG"; break
  fi
  sleep 120
done
echo "$(date '+%F %T') retry loop done" >> "$LOG"

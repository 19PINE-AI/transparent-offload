#!/usr/bin/env bash
# Opportunistic retry for the real remote-HSM measurement. The box SIGSTKFLT-kills
# CPU-heavy bursts at random, but a loop whose body is TRIVIAL (test + spawn-detached
# + sleep) survives them (this is why gpu_monitor lived through the bursts). Each
# attempt runs fully detached (setsid nohup, no wait), so a burst that kills an
# attempt does not touch this loop. Stops when remote_hsm_results.csv has a data row.
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
CSV=$ROOT/remote_hsm_results.csv
LOG=$ROOT/remote_hsm_run.log
rm -f "$CSV"
echo "$(date '+%F %T') retry loop start (trivial body, detached attempts)" > "$LOG"
for t in $(seq 1 200); do                      # patient: up to ~200 * 90s ~ 5 h
  sleep 90                                      # sleep FIRST so we never launch into the active burst
  if [ -s "$CSV" ] && [ "$(wc -l < "$CSV")" -ge 2 ]; then
    echo "$(date '+%F %T') SUCCESS on attempt window $t" >> "$LOG"; cat "$CSV" >> "$LOG"; break
  fi
  if ! pgrep -f remote_hsm_sweep.sh >/dev/null 2>&1; then
    echo "$(date '+%F %T') spawn attempt $t" >> "$LOG"
    setsid nohup bash "$ROOT/remote_hsm_sweep.sh" </dev/null >> "$LOG" 2>&1 &
  fi
done
echo "$(date '+%F %T') retry loop done" >> "$LOG"

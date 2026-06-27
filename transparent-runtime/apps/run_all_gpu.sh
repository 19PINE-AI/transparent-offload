#!/usr/bin/env bash
# One clean idle-GPU run. Apps FIRST (the main per-class 1MB result) so a short idle
# window still captures them; the block-size weight curves (appendix) follow.
# Touches a sentinel when all finish. Per-op GPU latency is recorded for self-validation.
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
rm -f "$ROOT/apps/.gpu_all_done"
bash "$ROOT/apps/app_gpu_sweep.sh"        # 7 apps @ 1MB AES (Redis/nginx/memcached/Node/Go/Apache/HAProxy)
bash "$ROOT/apps/aes_blocksize_sweep.sh"  # Node block-size weight curve (appendix)
bash "$ROOT/apps/aes_blocksize_py.sh"     # Python block-size weight curve (appendix)
touch "$ROOT/apps/.gpu_all_done"

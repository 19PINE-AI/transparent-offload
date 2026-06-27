#!/usr/bin/env bash
# One clean idle-GPU run: the AES block-size sweep (4KB->1MB on Node) + the 1MB-AES
# app sweep (Node/Go/Apache/HAProxy). Touches a sentinel when both finish.
ROOT=/home/ubuntu/transparent-offload/transparent-runtime
rm -f "$ROOT/apps/.gpu_all_done"
bash "$ROOT/apps/aes_blocksize_sweep.sh"
bash "$ROOT/apps/app_gpu_sweep.sh"
touch "$ROOT/apps/.gpu_all_done"

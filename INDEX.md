# Transparent Offload — evidence index

Navigate the whole project. Two bodies of work: a **correctness study** (when is it safe?)
and a **runtime + performance study** (does it win?), both measured.

## Claims → evidence

| Claim | Evidence | Where |
|---|---|---|
| Crypto path has ≈0 cross-connection conflicts | TSan on OpenSSL: 0 races data+handshake; only warm-up | `conflict-measurement/RESULTS.md` |
| **Session cache is race-free (lock-protected)** | real-socket TSan harness: 0 races (read/add/evict); nginx resumption works | `conflict-measurement/sess_socket_harness.c` |
| Conflicts are predictable by shared-state pattern | taxonomy: RO/per-conn/locked safe; unlocked counter/queue/cache hazard | `conflict-measurement/APPLICATION_CLASSES.md` |
| Thread-local state (errno/ERR) is a real coroutine hazard | 100% corruption naive, 0 with save/restore | `conflict-measurement/` `coop_err.c` |
| Coroutine hides offload latency (the core claim) | flat 453K±5K req/s over L=1–100µs (8 reps) | `runtime/fig_sweep_L_ci.png` |
| Beats busy-wait & context-switch | busy collapses 297K→9K; block capped 320K (128 thr); coro 453K (1 thr) | `runtime/sweep_L_reps.csv` |
| Coro Pareto-dominates block on latency too | lower p99 at every throughput, +46% max | `runtime/fig_pareto.png` |
| Win grows with finer-grained work | coro/block 2.5×→1.05× as W 0.5→16µs | `runtime/fig_wsweep_ci.png` |
| Block needs many threads, still caps | needs ≥8 threads, tops at 320K | `runtime/fig_blockthreads.png` |
| Robust to offload-latency jitter | coro throughput invariant over jitter 0–80µs | `runtime/RUNTIME_RESULTS.md` |
| Works on real GPU (mechanism) | FastWake fibers + cudaEventQuery; AES==OpenSSL; 4.8× busy, saturates GPU | `runtime/gpu_runtime.cu`, `gpu_clean.csv` |
| GPU offload regime is 11–45µs | AES latency-vs-batch, uncontended | `gpu/aes_gpu.cu` |
| Automatic batching = SSLShader for free | 4.4–4.9× for small requests | `gpu/batch_demo.cu` |
| Speculate-or-serialize is sound + cheap | 0 lost updates always; overhead 0; fallback tracks contention | `runtime/fig_detector.png`, `detector_reps.csv` |
| Detector profitable region pinned | free at low contention even w/ 50µs fallback | `runtime/RUNTIME_RESULTS.md` |
| Offload boundary is transparently interposable | unmodified binary, identical output, 64 calls intercepted | `transparency/` |
| **Transparent overlap on unmodified binary (M:N runtime)** | conn_server +LD_PRELOAD: 1.4-1.9x (emul), 17.3x (GPU), bad=0 | `transparent-runtime/TRANSPARENCY_RESULTS.md` |
| **Runtime safe on stock nginx; generalizes to 2nd binary** | nginx 10/10 0-failed; infer_server 11.8x bad=0 | `transparent-runtime/` |
| **Runtime transparent AND correct (lock-respect + save/restore)** | unlocked shared state corrupts (289k lost); fiber-mutex -> 0 lost, no deadlock; naive mutex deadlocks | `transparent-runtime/TRANSPARENCY_RESULTS.md` §6 |
| **Event-driven boundary, with evidence** | conn_server 64 fibers engage (17.3x); nginx 0 fibers (safe passthrough) | `transparent-runtime/` §7 |
| **Fused transparent conflict detector (Design A)** | page-protection: auto-detects 125k conflicts on hostile (0 on safe); enforce -> 0 lost, no app lock | `transparent-runtime/DESIGN_A_RESULTS.md` |
| **Real third-party binary works** | stunnel under runtime: 8/8 TLS correct, fibers engage; Apache safe; MariaDB needs fiber-cond/rwlock | `transparent-runtime/THIRD_PARTY_APPS.md` |

## Reading order
1. `EXPERIMENT_PLAN.md` — the full plan (GPU-updated).
2. `AUTONOMOUS_SESSION_SUMMARY.md` — what was built/measured this session.
3. `runtime/RUNTIME_RESULTS.md` — all performance numbers + figures.
4. `conflict-measurement/{RESULTS,APPLICATION_CLASSES}.md` — the correctness study.

## Build notes
- CUDA: `/usr/local/cuda-12.8/bin/nvcc -arch=sm_120` (Blackwell). PATH nvcc is 11.5, won't work.
- Emulated-accelerator experiments run under `sudo` (SCHED_FIFO on non-isolated cores).
- TSan study needs `sudo sysctl -w vm.mmap_rnd_bits=28` on this 6.8 kernel.

## Honest gaps (next, supervised)
- **[DONE]** Full transparent M:N runtime on unmodified binaries (`transparent-runtime/`): real LD_PRELOAD overlap + correctness on 5 binaries/configs incl. real GPU (17.3x) and stock-nginx safety. Boundary documented: overlap applies to thread-per-connection; event-loop servers run safely but need the runtime to own the loop for overlap.
- **[DONE]** Open-loop latency-vs-load generator (`runtime/openloop.{c,csv}`, `fig_openloop.png`): overlap holds p50+p99 low to ~4× the offered load of blocking (knee ~410K vs ~103K req/s). Source for the paper's latency figure.
- Real vLLM/Triton under TSan (Phase 5.1); wire runtime under nginx (Phase 3.1).

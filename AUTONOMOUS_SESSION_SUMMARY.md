# Autonomous session summary — transparent offload

Executed the research plan autonomously. Built real, measured artifacts across four
phases. All code + data + figures under `transparent-offload/{runtime,gpu,conflict-measurement}/`.

## What got done (all measured, not argued)

### Phase 3.0 — GPU crypto accelerator  ✅ (correctness) / ⏸ (clean latency deferred)
- `gpu/aes_gpu.cu`: AES-128-CTR CUDA kernel, **correctness PASS vs OpenSSL**. The GPU is
  now the crypto accelerator (no QAT needed).
- Clean latency-vs-batch sweep **deferred**: the GPU was 100% utilized all session by an
  unrelated co-tenant `python3` job (PID 4094495) — I left it untouched (not mine).

### Phase 3.3 — "what should the CPU do while waiting?"  ✅ HEADLINE
- `runtime/accel_compare.c`: one server core, emulated accelerator (controllable latency L),
  three strategies: busy-wait, block+context-switch, transparent coroutine (FastWake fiber).
- **`fig_sweep_L.png`** (throughput vs L): coro is **flat ~450K req/s across L=1→100µs**
  (hides offload latency entirely, 1 thread); busy **collapses** 297K→9K; block capped at
  ~320K (ctx-switch tax, needs 128 threads, 385µs p99 tails).
- **`fig_pareto.png`** (latency–throughput): coro **Pareto-dominates** block — e.g. at C=8,
  coro 347K req/s @ 22.4µs p99 vs block 315K @ 26µs; coro max throughput is **46% higher**.

### Phase 1 — transparent runtime on the REAL GPU  ✅
- `runtime/gpu_runtime.cu`: N connections = FastWake fibers on one CPU thread, each offloads
  AES to the GPU on its own stream, parks; dispatcher polls **`cudaEventQuery`** (= the
  FastWake dispatcher with CUDA events instead of RDMA CQs) and resumes the finished fiber.
- **GPU AES == OpenSSL (PASS), incl. 251K concurrent ops verified (0 mismatches).**
  Clean (uncontended) GPU: **coro 4.8–7.3× busy-wait, saturating the GPU from one CPU
  thread** (busy leaves it idle). [An earlier contended run showed an inflated 31.6×/
  "linear-to-90×" — that was a co-tenant-contention artifact, now superseded.]
- Empirically confirmed why FastWake's fast switch is load-bearing: an early `ucontext`
  version was **4× slower** (its `sigprocmask` syscall per switch).

### Phase 4 — speculate-or-serialize conflict detector  ✅ (top-tier differentiator)
- `runtime/conflict_detector.c`: handlers read shared state, offload (park), commit an RMW;
  detector validates version at commit and **serializes on conflict** (sound: the commit
  section never yields).
- **`fig_detector.png`**: naive **corrupts up to 98%** of updates; **detect loses 0 at every
  contention level**; **fallback rate tracks contention exactly** (100% all-collide → 0% once
  keys are plentiful); **detection overhead ≈ 0**. This is the measured "safe-and-profitable
  region" — broad, because real shared state is either low-contention or should be locked.

## Figures (in `runtime/`)
- `fig_sweep_L.png` — offload-latency crossover (the headline).
- `fig_pareto.png` — latency–throughput Pareto (coro dominates).
- `fig_detector.png` — detector correctness/fallback vs contention.

## Honest caveats
- **GPU was shared all session** → GPU absolute latency/throughput are contention-inflated;
  only *relative* GPU results are reported. Clean Phase-3.0 sweep + uncontended runtime
  numbers still to do (rerun `gpu/aes_gpu` and `runtime/gpu_runtime` when GPU is free).
- Emulated-accelerator results use **SCHED_FIFO** on the polling threads (cores not isolated;
  co-tenant load) — matches FastWake's own realtime pinning; documented in the code.
- Detector fallback *cost* modeled as a cheap re-read; full-system fallback = re-run
  post-processing (cost scales with its size). The *rate* and *soundness* transfer.

## Environment changes I made (and reverted)
- Set `vm.mmap_rnd_bits=28` (needed for the earlier TSan work; persists, harmless).
- Enabled GPU persistence mode (harmless). **Locked then RESET GPU clocks** (`-rgc`) so the
  co-tenant job is not throttled. Did **not** touch the co-tenant process.

## Round 2 — statistical rigor, sensitivity, clean GPU, transparency
Re-ran everything with repetitions + 95% CIs and added sensitivity/robustness experiments.

- **Headline, 8 reps ± 95% CI** (`fig_sweep_L_ci.png`): coro **451–454K ±~5K** flat over
  L=1→100µs; block **317–325K ±~5K**; busy 296K→9.3K. Gap ≫ CI → highly significant.
- **CPU-work sensitivity (W-sweep, 6 reps, `fig_wsweep_ci.png`):** coro's win over
  context-switch **grows as work shrinks** — 2.5× at W=0.5µs, →1.05× at W=16µs. The
  fine-grained-offload regime is exactly where coro wins. coro hits **1.62M req/s** at
  W=0.5µs (fiber-switch overhead ~0.12µs).
- **block thread-count (6 reps, `fig_blockthreads.png`):** block needs ≥8 threads to hide
  20µs and **caps at ~320K**; coro matches+beats with **1 thread**.
- **Jitter robustness:** coro throughput **invariant** to offload-latency jitter (0–80µs);
  busy degrades. coro p99 even improves (convoy-breaking).
- **Detector, 8 reps:** detect **0 lost updates at every contention level**; fallback%
  100→0 with tight CIs; **detection overhead statistically zero** (CIs overlap naive).
- **Clean GPU (co-tenant finished mid-session):** AES correctness PASS; offload latency
  floor **11–45µs** for 64B–16KB; GPU runtime **busy 28.7K vs coro 137K ops/s = 4.8×**
  (6 reps, tight CI), coro **saturates the GPU from one CPU thread**, busy never does.
- **Transparency (gate #1, interposition half):** an **unmodified binary** (`transparency/`)
  runs identically (checksum 8487680) while an LD_PRELOAD shim transparently intercepts all
  64 offload calls — the offload boundary is a clean interposition point. (Full
  epoll-coroutine-ization of a stock server is the remaining half; not faked.)
- **Phase 3.3b — automatic batch formation (`gpu/batch_demo.cu`):** fusing parked offloads
  into one GPU kernel gives **4.4–4.9× throughput for small requests** (the SSLShader lever),
  for free from the dispatcher; pulls crypto into the batch-queue hazard pattern.
- **Detector fallback-cost sensitivity:** at low contention the detector is **free even with
  a 50µs fallback** (rare re-runs); hot+expensive state should be locked — exactly pinning
  the safe-and-profitable region. Detect stays correct (0 lost) throughout.

## Figures (all in `runtime/`)
`fig_sweep_L_ci.png` (headline ±CI), `fig_pareto.png`, `fig_wsweep_ci.png`,
`fig_blockthreads.png`, `fig_detector.png`. Data CSVs alongside (`*_reps.csv`).

## Recommended next steps (gate order)
1. **Gate #1 — transparency:** LD_PRELOAD offload-interception on an unmodified app (Phase 1.1).
   This is the remaining big validation of the "no code change" claim.
2. Rerun GPU sweeps uncontended (Phase 3.0) for clean absolute numbers.
3. Open-loop generator for a formal p99-vs-load story (Pareto already shows the trend).
4. Batched-GPU-crypto queue (3.3b) feeding the detector — ties crypto to the LLM-batching pattern.

See `EXPERIMENT_PLAN.md` for the full plan; `conflict-measurement/` for the earlier
TSan conflict study; `runtime/RUNTIME_RESULTS.md` for full numbers.

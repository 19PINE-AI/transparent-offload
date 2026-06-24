# Phases 1–6 complete — autonomous session

All remaining phases executed and measured. Below, each phase with its headline result and
artifacts. Builds on the earlier microbenchmark + correctness studies (`INDEX.md`).

## Phase 1 — real transparent runtime, real sockets (`runtime/tco_server.c`)
A real epoll + FastWake-fiber coroutine runtime over real TCP. Handler is plain synchronous
(`read → offload → write`); offload and socket I/O are both yield points.
- **coro hides offload latency** (flat ~295K rps across L=5–50µs); block degrades; **win
  1.55→2.43× growing with L**; `bad=0` (transform verified end-to-end). (`TCO_RESULTS.md`)

## Phase 1b — real GPU AES offload (`runtime/tco_server_gpu.cu`)
`co_offload` = real GPU AES-CTR + `cudaEventQuery`-polled dispatcher.
- **coro 18.5× block**, end-to-end **AES verified (`bad=0`)**; block crippled by per-request
  `cudaStreamSynchronize`. The whole stack works: real epoll + TCP + GPU crypto + sync handler.

## Phase 2 — correctness at scale
- **Pipelined, order-dependent requests** (depth 1/8/32): `bad=0` across ~340–375K req/config —
  fd-partition serializes each connection's dependent requests (the "msg #2 depends on #1" case).
- **AddressSanitizer** on the runtime: **0 errors** over 258K requests.

## Phase 3 — latency rigor
- **Pareto with CIs** (5 reps, `fig_pareto_ci.png`): coro Pareto-dominates block on both axes.
- **Open-loop** (Poisson, `fig_openloop.png`): coro holds p50 ≈ 24–33µs to ~360K offered;
  **block's knee is ~170K** (then p50 → 250–340ms). coro sustains low latency to ~3× the load.

## Phase 4 — detector vs CORRECT lock baseline (`fig_detector_vs_lock.png`)
- Low contention: **detector ≈ lock** (within 8%). High contention: **detector 4–67× faster** —
  locking serializes the offloads (defeating transparent offload) while the detector keeps them
  overlapped. Closes the audit gap ("only compared to broken naive").

## Phase 5 — risky class verdict: DNN/LLM inference (`conflict-measurement/PHASE5_INFERENCE.md`)
- **vLLM 0.19 source inspection:** scheduler + paged-KV-cache allocator use **ZERO locks**
  (rely on single-threaded run-to-completion) — the taxonomy hazard, in production.
- **Skeleton under TSan:** unlocked → **10 races, 46,178 double-allocated KV blocks, SEGFAULT**;
  locked → 0/0/clean. The ⚠️ is now a measured ❌-without-protection: this class *requires* the
  detector or locking.

## Phase 6 — ablations + related work (`ABLATIONS_AND_RELATED.md`)
- **Ablation table:** each mechanism justified by a measured failure when removed (fast switch
  20.2× via `switchcost.c`; save/restore 100% corruption; pre-warm 6/12 cold races; fd-partition
  1.26M lost; detector 46K double-alloc + segfault).
- **Related work** positioned with measured deltas (State Threads ucontext = 20× our switch;
  SSLShader overlap matched + batching for free without rewrite; Cimple needs annotation; etc.).

## New artifacts this session
Code: `tco_server.c`, `tco_server_gpu.cu`, `openloop.c`, `switchcost.c`,
`conflict-measurement/{sess_socket_harness.c, inference_skel.c}`, lock mode in
`conflict_detector.c`. Figures: `fig_pareto_ci.png`, `fig_openloop.png`,
`fig_detector_vs_lock.png`. Docs: `TCO_RESULTS.md`, `PHASE5_INFERENCE.md`,
`ABLATIONS_AND_RELATED.md`, this file.

## Honest caveats (carried forward)
- Full LD_PRELOAD transparency on a *stock* unmodified server binary (vs our plain-synchronous
  handler against the runtime) remains the one not-fully-closed item; the handler-is-synchronous
  transparency is demonstrated, symbol interposition is demonstrated, but the two aren't yet
  fused on an unmodified third-party binary.
- Open-loop p99 tails are inflated by the dispatcher's O(POOL) scan (a ready-queue would fix);
  p50 + achieved-rate are the clean signals.
- GPU absolute numbers vary with co-tenant contention; ratios and the clean-window numbers hold.

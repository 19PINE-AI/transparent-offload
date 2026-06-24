# Phase 6 — ablations + related-work positioning (measured)

## Ablations: what breaks when each mechanism is removed
Each runtime mechanism is justified by a measured failure when disabled.

| Mechanism | Disabled → failure (measured) | Evidence |
|---|---|---|
| **Fast user-level switch** (vs `ucontext`) | switch costs **977 ns vs 48 ns = 20.2× slower**; full runtime ~4× slower with ucontext | `switchcost.c`, gpu_runtime note |
| **Per-thread state save/restore** (errno/ERR) | **~100% corruption** (399,999/400,000) of post-offload reads | `coop_err.c` |
| **Pre-warm lazy global caches** | warm-up races in **6/12 cold starts** (0/12 warm) on OpenSSL method store | `conflict-measurement/RESULTS.md` |
| **fd-partition** (serialize same-connection) | order-dependent updates lost — NKEY=1: **1.26M lost** (naive) vs 0; KV-block double-alloc | `conflict_detector.c`, `inference_skel.c` |
| **Lock-respect / conflict detector** | **46K double-allocated KV blocks + SEGFAULT**; lost updates up to 98% | `inference_skel.c`, `conflict_detector.c` |

Conversely, each mechanism *present* is shown correct: save/restore → 0 corruption; pre-warm
→ 0 races; fd-partition → pipelined order-dependent requests `bad=0` (`tco_server`); detector
→ 0 lost updates at every contention level.

## Related work — positioned with measured deltas
- **State Threads (SGI) / GNU Pth** — transparent coroutine over an event loop, the closest
  prior art. They switch via `ucontext`/`setjmp` (**measured 977 ns/switch = 20× ours**), and
  they hide *blocking syscalls*, not *accelerator offload* — no offload-completion polling, no
  conflict detection. Our delta: a 48 ns switch + an offload-yield (epoll **and** `cudaEventQuery`)
  + the speculate-or-serialize detector for the concurrency they would silently break.
- **Cimple / Psaropoulos et al.** — coroutines to hide memory/storage latency, but require
  manual code annotation. We are transparent: the handler is plain synchronous code, measured
  `bad=0` end-to-end.
- **async-OpenSSL / SSLShader (NSDI'11, GPU crypto)** — hand-written async + manual batching
  (the performance upper bound). We match the GPU overlap (coro **saturates the GPU** from one
  CPU thread) AND get batching **for free** (**4.4–4.9×**, measured) — without the rewrite
  SSLShader required. Our handlers are unmodified synchronous code.
- **Shenango / Caladan** — reallocate whole cores at µs scale (hand the core to another app).
  Orthogonal: we keep useful work *in-thread* via offload overlap rather than yielding the core.

**The open cell** (transparent + correct + accelerator-offload overlap) is occupied with
measured support on each axis: transparency (`tco_server` plain handler, `bad=0`), correctness
(detector + session-cache + fd-partition results), and offload overlap (the latency-sweep,
Pareto, open-loop, and real-GPU results).

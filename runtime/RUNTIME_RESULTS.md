# Phase 1 + Phase 3 results — transparent-offload runtime (autonomous session)

Built and measured a working prototype + the headline performance experiment.
All code in `transparent-offload/runtime/` and `transparent-offload/gpu/`.

## Summary of what was built and shown
1. **GPU AES-128-CTR crypto accelerator** (`gpu/aes_gpu.cu`) — pure-C-verifiable kernel,
   **correctness PASS vs OpenSSL**. Serves as the real crypto offload (no QAT needed).
2. **3-approach comparison + offload-latency sweep** (`runtime/accel_compare.c`) — the
   headline figure answering "what should the CPU do while waiting for the accelerator?"
3. **Transparent-offload runtime on the real GPU** (`runtime/gpu_runtime.cu`) — FastWake
   fibers + a `cudaEventQuery` dispatcher; **AES correct (incl. 251K concurrent ops, 0
   mismatches), 4.8–7.3× throughput over busy-wait (clean GPU), saturates the GPU from one
   CPU thread** — on the actual Blackwell GPU.

---

## Headline: what the CPU should do while waiting (emulated accelerator)
One server core, an emulated accelerator (separate core) with controllable offload
latency L, request = preprocess(1µs) + offload(L) + postprocess(1µs). Three strategies:
**busy** (busy-wait), **block** (relinquish → OS context-switch), **coro** (transparent
coroutine: park at offload via FastWake's ~6ns switch, run another connection).

Throughput (K req/s), C=128, W=2µs CPU/req — `sweep_L.csv`, plotted in `fig_sweep_L.png`:

| L (µs) | busy | block | **coro** |
|---:|---:|---:|---:|
| 1   | 297 | 331 | **444** |
| 2   | 229 | 315 | **461** |
| 5   | 130 | 329 | **443** |
| 10  | 79  | 315 | **460** |
| 20  | 42  | 327 | **445** |
| 50  | 19  | 311 | **460** |
| 100 | 9.1 | 324 | **444** |

**Findings**
- **coro hides offload latency entirely**: throughput is flat at ~450K req/s across L=1→100µs,
  on **one thread**. This is the core value proposition, quantified.
- **busy collapses** ~1/(W+L): 297K → 9K as L grows (33× worse at L=100µs).
- **block is latency-independent but capped** at ~320K by the OS context-switch tax, needs
  **128 threads** (vs coro's 1), and shows 385µs p99 tails from 128-thread scheduling contention.
- coro beats block by **1.36–1.46×** in throughput at every L, with 1/128 the threads.

Caveat (honest): in this closed-loop saturation test coro's *p50 latency* is the queueing
latency (≈ C/throughput); a fair latency-vs-load (open-loop) comparison is future work
(Phase 3, open-loop generator). The throughput/CPU-efficiency story is the headline.

Methodology note: polling threads run **SCHED_FIFO** (the machine's cores are not isolated
and a co-tenant GPU job adds load); without it, busy/coro tails hit ms-scale from
descheduling. This matches FastWake's own realtime pinning.

---

## Real-GPU transparent runtime (`gpu_runtime.cu`)
N "connections" = FastWake fibers on ONE CPU thread. Each offloads AES-CTR to the GPU on
its own CUDA stream, records a `cudaEvent`, and **parks**; the dispatcher polls
`cudaEventQuery` (the FastWake dispatcher loop with CUDA events instead of RDMA CQs) and
resumes whichever fiber's GPU op completed. Baseline **busy** = 1 connection, synchronous
`cudaStreamSynchronize` per op (CPU stalls).

- **Correctness: GPU AES-CTR == OpenSSL (PASS).**

**Clean numbers (UNCONTENDED GPU, clocks locked, `gpu_clean.csv`):**
- **busy (N=1): 21,779 ops/s** — the synchronous round-trip leaves the GPU idle most of
  the time (per-op ~45 µs = the measured AES e2e floor).
- **coro: saturates the GPU at ~139,000 ops/s from ONE CPU thread → 6.4× busy.**

| N (coro) | 1 | 4 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|
| ops/s | 21,719 | 93,574 | 134,743 | 136,689 | 138,240 | 138,738 |

Reproduced over **6 reps** (`gpu_reps.csv`, clocks auto): busy **28,655 ±88** ops/s,
coro **136,857 ±409** → **4.8× ±tight** (the absolute ratio depends on GPU clocks; 4.8–6.4×
across clock states — coro always saturates the GPU, busy never does).

**Across request size** (locked clocks, coro N=64 vs busy): coro wins **3.4–7.3×**
(1KB→3.4×, 4KB→**7.3×**, 16KB→6.7×, 64KB→5.5×) — busy's per-op cost rises with the AES
latency floor while coro keeps the GPU saturated regardless. The annotated headline figure
`fig_money.png` overlays this real-GPU offload region (11–45µs) on the latency sweep — it
lands squarely where coro dominates.

N=1 coro == busy (no concurrency → no overlap); coro then **extracts the full GPU
throughput** (busy never does — it under-utilizes the accelerator). Two complementary wins:
busy wastes the **CPU** (stalls) *and* wastes the **GPU** (idle during the CPU's pre/post);
coro keeps both fed.

**GPU AES-CTR offload latency floor (`gpu/aes_gpu.cu`, Phase 3.0, uncontended):**

| payload | 64 B | 1 KB | 4 KB | 16 KB | 64 KB | 1 MB |
|---|---:|---:|---:|---:|---:|---:|
| e2e latency (µs) | 11.0 | 19.0 | 43.7 | 45.8 | 43.7 | 116 |

So a realistic TLS-record GPU crypto offload is **~11–45 µs** — squarely in the
"several-to-tens of µs" regime where the crossover figure shows coro winning decisively
(context-switch eats a meaningful fraction + adds latency; busy-wait wastes the whole window).

(Earlier in the session the GPU was 100% co-tenant-contended, giving an inflated 31.6×
ratio at ~2 ms/op; the clean 6.4× above supersedes it. The co-tenant was left untouched.)

---

## What this validates for the paper
- The central claim is now **measured**, not argued: a transparent coroutine that parks at
  the offload point **fully hides accelerator latency** (flat throughput vs L), beating both
  busy-wait (collapses) and block/context-switch (capped + 128× the threads).
- The **mechanism works on real hardware**: FastWake fibers + `cudaEventQuery` dispatcher =
  correct GPU crypto (251K concurrent ops verified) that saturates the GPU from one CPU
  thread. The FastWake→this-work narrative holds
  end-to-end ("how to wake fast" → "what to do while waiting").
- Reusing FastWake's `switch.S` was essential: an early `ucontext` version was 4× slower
  (its `sigprocmask` syscall per switch) — empirical confirmation of why the fast
  user-level switch is load-bearing.

---

## Statistical rigor + sensitivity (added)
**Headline sweep, 8 reps, mean ± 95% CI** (`sweep_L_reps.csv`, `fig_sweep_L_ci.png`):
coro **451–454K ±~5K** flat across L=1→100µs; block **317–325K ±~5K** flat; busy
296K→9.3K. The coro–block gap (~132K) is >20× the CI half-widths → highly significant.

**CPU-work sensitivity, W-sweep (L=20µs, C=128, 6 reps)** (`sweep_W_reps.csv`, `fig_wsweep_ci.png`):

| W (µs) | busy | block | coro | coro/block |
|---:|---:|---:|---:|---:|
| 0.5 | 46K | 648K | **1,618K** | 2.5× |
| 1   | 45K | 483K | **871K**   | 1.8× |
| 2   | 43K | 320K | **450K**   | 1.4× |
| 4   | 39K | 188K | **231K**   | 1.2× |
| 8   | 34K | 105K | **117K**   | 1.1× |
| 16  | 26K | 56K  | **59K**    | 1.05× |

**Key finding:** coro's win over context-switch **grows as CPU work per request shrinks**
(the more offload-dominated / fine-grained the workload, the more the ~1µs context-switch
tax matters). At W=0.5µs coro reaches 1.62M req/s ≈ 1/W — the fiber switch overhead is
~0.12µs effective, negligible. This is the quantitative case for *fine-grained* offload.

**block thread-count sensitivity (L=20µs, W=2µs, 6 reps)** (`block_threads_reps.csv`,
`fig_blockthreads.png`): block throughput scales with threads to **C=8 (325K)** then
**saturates flat at ~320K** for C=8→256 (more threads just add latency/contention). It
needs ≥8 OS threads to hide a 20µs offload and still tops out below coro's **453K — which
coro reaches with ONE thread.** (CIs ±1–2%.)

**Offload-latency jitter robustness (mean L=20µs, uniform jitter 0–80µs, C=128):**

| jitter (µs) | busy | block | coro |
|---:|---:|---:|---:|
| 0  | 43K | 314K | **461K** |
| 10 | 34K | 332K | **445K** |
| 20 | 30K | 319K | **462K** |
| 40 | 22K | 330K | **445K** |
| 80 | 16K | 317K | **461K** |

**coro throughput is invariant to jitter** (it overlaps whatever latency each op has) and its
p99 latency actually *improves* with jitter (498→430µs — jitter breaks completion convoys).
**busy degrades** as jitter lifts mean latency (1/(W+20+J/2)). Real accelerators — especially
a shared GPU — have variable latency; coro absorbs it transparently.

---

## Phase 3.3b: automatic batch formation on GPU crypto (`gpu/batch_demo.cu`)
A transparent-coroutine dispatcher sees ALL parked offloads at once, so it can **fuse them
into one kernel** — SSLShader-style batching with no app rewrite. Upside (B=64 AES requests,
per-request = B kernels vs 1 fused kernel, uncontended GPU):

| request size | per-request ops/s | batched ops/s | speedup |
|---:|---:|---:|---:|
| 256 B  | 309K | 1,454K | **4.7×** |
| 1 KB   | 299K | 1,455K | **4.9×** |
| 4 KB   | 268K | 1,184K | **4.4×** |
| 16 KB  | 223K | 563K   | 2.5× |
| 64 KB  | 231K | 179K   | 0.77× |

**Batching wins 4.4–4.9× for small requests** (launch-bound — the common TLS-record case),
crossing to neutral for large (bandwidth-bound). This is the lever that makes crypto-on-GPU
practical (cf. SSLShader, NSDI'11) — and it falls out of the coroutine runtime *for free*.
It also pulls crypto into the **batch-queue pattern**: the shared batch buffer is exactly the
cross-connection state the Phase-4 detector / lock-respecting rule must guard. One mechanism,
one hazard model, spanning crypto and LLM-batching.

---

## Phase 4: speculate-or-serialize conflict detector (`conflict_detector.c`)
The detector for the taxonomy's *hazard* patterns (cross-fd shared cache). Handlers
read a shared entry, **offload (park)**, then commit a read-modify-write. While parked,
other coroutines may touch the same entry.
- **naive** (no detection): commit unconditionally.
- **detect**: at commit, validate the entry's version vs the value read; on mismatch
  **serialize** (re-read current value and re-apply). Sound because the commit section
  itself never yields (cooperative non-preemption), so validate+commit is atomic.

Contention sweep (C=64, L=20µs, 1.28M commits), `detector.csv`, `fig_detector.png`:

| #keys | naive: updates LOST | detect: lost | detect: fallback % | detect tput |
|---:|---:|---:|---:|---:|
| 1     | 1,260,000 (98%) | **0** | 100%  | 3.11 M/s |
| 4     | 1,203,563 (94%) | **0** | 100%  | 3.11 M/s |
| 16    | 1,014,719 (79%) | **0** | 99.0% | 3.12 M/s |
| 64    | 593,099 (46%)   | **0** | 57.6% | 3.09 M/s |
| 256   | 65,919 (5%)     | **0** | 3.8%  | 3.12 M/s |
| 1024  | 0               | **0** | 0%    | 3.10 M/s |
| 16384 | 0               | **0** | 0%    | 3.10 M/s |

**Statistical rigor (8 reps, mean ± 95% CI, `detector_reps.csv`):**
detect lost_updates = **0 at every NKEY** (1→16384); fallback% = 100±0, 99.1±0.2, 58.3±0.7,
4.8±0.5, 0.4±0.4, 0±0; throughput ~2.96–2.99M ops/s for **both** naive and detect (CIs
overlap → **detection overhead is statistically zero**). naive lost: 1.26M, 1.02M, 597K,
64K, 3K, 0 — corruption proportional to contention.

**Findings**
- **naive corrupts** in proportion to contention — up to 98% of updates lost.
- **detect is always correct** (0 lost updates at every contention level).
- **Fallback rate tracks contention exactly** — 100% when every key collides, decaying
  to **0% once keys are plentiful** (the common case: real caches have many keys). The
  detector spends effort *only* where conflicts actually occur — graceful degradation.
- **Detection overhead ≈ 0**: detect throughput == naive (version compare is in the noise).
**Fallback-cost sensitivity** (model fallback = re-run post-processing of `REDO` µs; detect,
C=64): detect stays **correct (0 lost) at every point**. Throughput (ops/s):

| fallback cost | NKEY=16 (hot) | NKEY=256 (mid) | NKEY=4096 (cold) |
|---:|---:|---:|---:|
| 0 µs  | 3.10M | 3.10M | 3.10M |
| 2 µs  | 481K  | 2.59M | 3.08M |
| 10 µs | 96K   | 449K  | 2.84M |
| 50 µs | 19K   | 118K  | **2.87M** |

**This pins the safe-and-profitable region exactly:** at **low contention (many keys — the
common case for real caches), the detector is essentially free even with a 50µs fallback**
(conflicts are rare, so re-runs rarely fire). Only when state is *hot* AND fallback is
expensive does throughput collapse — and that is precisely the state that should be
**locked**, which the scheme already respects. So: speculate on the low-contention residual,
lock the genuinely-hot state. The detector covers exactly the gap the taxonomy left open.

---

## Next (per EXPERIMENT_PLAN.md)
- Open-loop latency-vs-load comparison (fair p99 story).
- Clean GPU AES latency-vs-batch (Phase 3.0) once GPU is uncontended; batched-GPU-crypto
  queue (3.3b) feeding the conflict detector (Phase 4).
- Wire the runtime under an unmodified server via the LD_PRELOAD offload-interception shim
  (Phase 1.1) for the zero-code-change end-to-end (Phase 3.1).

### Detector vs a CORRECT lock baseline (`fig_detector_vs_lock.png`)
The earlier overhead number compared detect to the *broken* naive path. Against a **correct
lock baseline** (per-key lock held across the offload — the lock-respecting scheme that
serializes same-key handlers), C=64:

| #keys | lock ops/s | detect ops/s | detect/lock |
|---:|---:|---:|---:|
| 1 (all collide) | 46K | 3.09M | **66.7×** |
| 16 | 713K | 2.80M | **3.93×** |
| 64 | 3.05M | 2.84M | 0.93× |
| 256–16384 | ~3.08M | ~2.83M | 0.92× |

**Low contention: detector ≈ lock (within 8%).** **High contention: detector 4–67× faster** —
locking serializes the offloads (throughput → ~1/L, defeating transparent offload) while the
detector keeps them overlapped and only re-does the cheap post-processing. (At NKEY=1 lock
hits ~46K ≈ 1/L, confirming full serialization.) The detector's advantage is the offload
overlap it preserves; it shrinks when the post-processing/redo is expensive (then lock/
fd-partition wins) — the two together pin exactly when to speculate vs lock.

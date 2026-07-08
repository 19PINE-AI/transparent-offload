# Transparent Offload — experimental plan to a strong research contribution

## Where we are (foundation, done)
- **Crypto path** (TSan OpenSSL): ≈0 steady-state cross-connection conflicts; only
  transient warm-up races in the alg-fetch cache (pre-warmable); session cache is locked.
- **Cooperative-coroutine hazard** (errno/ERR): ~100% corruption naive, 0 with
  save/restore at yield → save/restore is a *mandatory* runtime component.
- **Compression** (TSan zlib): ≈0 library conflicts; only unlocked app-level state races.
- **Pattern taxonomy**: safe = {read-only, per-connection, lock-protected};
  hazard = {unlocked counter/queue/cache}. App-class mapping + predictive rule.

These establish **feasibility + a correctness frame**. They are characterization,
not yet a system. The paper needs: a working transparent runtime, end-to-end wins,
and a principled correctness mechanism with measured overhead.

---

## Hardware (this machine)
- **GPU:** NVIDIA RTX PRO 6000 Blackwell, 96 GB, compute cap 12.0 (sm_120),
  driver 595. Build CUDA with **`/usr/local/cuda-12.8/bin/nvcc -arch=sm_120`**
  (the PATH nvcc is 11.5 and cannot target Blackwell). torch 2.10+cu128 works.
- No QAT card → **the GPU is the single accelerator for BOTH crypto and inference.**
  This is a feature, not a compromise: one device, two app classes, and crypto-on-GPU
  naturally needs batching (cf. **SSLShader**, NSDI'11), which drives the *exact*
  shared-state hazard the taxonomy predicts — so the same crypto app can be dialed
  between the safe and risky regimes.

## Infrastructure to build first (shared by everything below)
1. **GPU accelerator service** (the real headline accelerator, for crypto AND inference):
   - **GPU crypto kernel** — bulk **AES-CTR/GCM** over caller buffers (start here;
     simple, deterministic, verifiable vs OpenSSL). Optionally **RSA/modexp batched**
     for the handshake (where SSLShader's big win was, and where batching is mandatory).
   - **GPU inference** — a stateless model (ResNet/ViT or a small BERT) via torch/TensorRT.
   - **Async submit + completion→resume:** submit on a CUDA stream, record a CUDA event,
     `coro_yield`; the per-core dispatcher **polls `cudaEventQuery`** (or `cudaLaunchHostFunc`)
     and switches the coroutine back in on completion — *exactly the FastWake dispatcher
     loop with CUDA events in place of RDMA CQs.* (Tie this mechanism out explicitly.)
   - **Two offload modes (the key knob):** **per-request** (one kernel/op, clean
     per-connection state → *safe* pattern) vs **batched** (a shared batch queue
     accumulates many connections' ops into one kernel → *queue* pattern → needs the
     detector). Same app spans both regimes.
2. **Emulated accelerator** — CPU-side submit/complete with knobs: offload latency
   (**0.5–100 µs incl. sub-µs**, which the GPU's ~10–30 µs fixed launch+PCIe floor
   cannot reach), queue depth, jitter. Completion via FastWake fast-wakeup. The
   workhorse for **offload-latency sweeps** independent of GPU fixed costs.
3. **Runtime built on FastWake primitives**: reuse the 6 ns fiber `switch_to`
   (`lib/switch.S`), the per-core dispatcher (`lib/runtime.c`), and the LD_PRELOAD
   shim (`shim/shim.c`). Major build-cost saver and a clean narrative
   ("FastWake answered *how to wake fast*; this answers *what to do while waiting*").
4. **Load generators**: wrk2/h2load (TLS), a compression client, an inference client.
5. **Conflict-injection harness** + the existing TSan harnesses as regression gates.

> **GPU-specific notes to bake in.** (a) GPU per-op latency is **tens of µs**, so to
> hide it you need **many in-flight events** → more cross-connection interleaving →
> the conflict question matters *more*, not less. (b) The CUDA submit path has its own
> internal locks (CUDA runtime is thread-safe) → the runtime must respect them
> (lock-respect rule) and likely funnel GPU submits through a per-core dispatcher
> rather than N coroutines calling CUDA concurrently. (c) Verify every GPU crypto
> result against OpenSSL on CPU (correctness oracle).

---

## Phase 1 — Build the transparent runtime (core systems contribution)
Goal: take an **unmodified** event-driven app and make its offload calls yield.

- **1.1 Offload interception + yield.** LD_PRELOAD shim intercepts (a) the offload
  submit (libcrypto EVP / accelerator driver symbol) → submit-async + `coro_yield`;
  resume on completion; (b) `epoll_wait`/`io_uring` → deliver each ready event as a
  new coroutine. *Success:* unmodified `nginx` binary runs; offloads yield not block.
  *Risk/gate:* if a clean transparent offload-interception point doesn't exist for a
  real app, fall back to "recompile with a one-line offload macro" framing.
- **1.2 fd-partition scheduler + lock-respect.** Partition events by fd; serialize
  same-fd; pause a coroutine that blocks on a mutex held by another (intercept
  `pthread_mutex_lock`). *Success:* correctness preserved under load.
- **1.3 Per-thread state save/restore at yield** (errno/ERR/DRBG). *Success:* the
  `coop_err` check, re-run *inside the real runtime*, shows 0 corruption.

**Deliverable:** Fig "architecture"; a demo of unmodified nginx+OpenSSL on the runtime.

---

## Phase 2 — Correctness at scale (preempt the #1 reviewer attack)
- **2.1 Differential testing.** Unmodified nginx+OpenSSL HTTPS under runtime vs vanilla:
  byte-identical responses over millions of requests, many connections, **including
  dependent pipelined requests** (the two-messages-on-one-fd case from the notes).
- **2.2 Sanitizers on the real app under the runtime:** zero *new* races introduced.
- **2.3 Conflict injection.** A module with unlocked shared state: naive coroutine
  corrupts; **detector + speculate-or-serialize fallback preserves correctness**.
- **2.4 Adversarial TLS:** pipelining, renegotiation, session **resumption** (drives
  the real cross-connection session-cache path end-to-end).

**Deliverable:** Table "correctness: N requests, 0 mismatches, 0 new races".

---

## Phase 3 — End-to-end performance (does it actually win?)
Baselines (from your notes): **(a)** block + OS context-switch, **(b)** busy-wait,
**(c)** transparent offload (ours), **(d)** hand-written async GPU integration
(**SSLShader-style**: manual batching + explicit pipelining) as the upper bound ours
should approach **with zero code change**.

- **3.0 GPU crypto microbench (do first — sets the regime).** Measure real GPU AES
  kernel **latency vs batch size** and throughput, plus the kernel-launch+PCIe floor.
  Output: the latency–batch curve that *motivates batching* and pins the offload-latency
  regime (expect ~10–30 µs/op floor, amortized by batching).
- **3.1 Headline:** nginx+OpenSSL HTTPS (bulk AES on GPU) throughput + latency
  (p50/p99/p99.9) across (a)–(d), sweeping **connections-per-core > 1**.
  *Hypothesis:* ours ≈ (d) ≫ (a),(b), with zero app code change.
- **3.2 CPU efficiency:** req/s per core, % core wasted waiting (the GPU-wait the CPU
  reclaims). With GPU's tens-of-µs latency the busy-wait waste is large → big win headroom.
- **3.3 Offload-latency sensitivity (the key figure).** Use the **emulated accelerator**
  to sweep 0.5–100 µs and find the crossover where ours beats context-switch; overlay
  the **real GPU operating point**. *Hypothesis:* ours wins across the whole fine-grained
  range (switch is tens of ns vs µs). The quantitative "why not just context-switch / busy-wait".
- **3.3b Batching knob (crypto on GPU, the new lever).** Same crypto app, two modes:
  **per-request** (safe, per-connection) vs **batched** (shared batch queue). Show
  transparent offload **forms batches automatically** via the scheduler (no SSLShader-style
  app rewrite) and measure latency/throughput vs batch size. This is also the input to 4.3.
- **3.4 Generality:** repeat 3.1 on a **compression server** (CPU or GPU) and a
  **stateless GPU inference server** (ResNet/ViT/BERT) — both safe classes, same win shape.
- **3.5 Scaling:** throughput vs connections-per-core, vs #cores, and vs **GPU streams /
  MPS** (how many CPU connections one GPU + dispatcher can saturate).

**Deliverables:** Figs latency/throughput vs baselines; offload-latency crossover;
CPU-efficiency; multi-app generality.

---

## Phase 4 — The conflict detector (top-tier differentiator)
This is where the *research* (vs engineering) lives. Soundness via "always able to
serialize".

- **4.1 Build speculate-or-serialize.** Track the post-offload region's read/write
  set (shadow memory or page-protection), restricted to non-stack, non-per-fd memory;
  on a cross-coroutine conflict, **serialize instead of speculate** (always safe).
  Buffer external output (network send) until commit in fd-order.
- **4.2 Overhead.** Detection cost per event vs offload latency hidden. *Success:*
  overhead ≪ offload latency on target workloads.
- **4.3 Profitability vs contention (decides workshop vs top-tier).** Two testbeds:
  (i) the **real batched GPU-crypto queue** from 3.3b — a genuine instance of the
  queue pattern, not a synthetic one; (ii) a hostile app with tunable shared-state
  contention. Show graceful degradation (more fallback as contention rises), **never
  corrupts**, and chart the win vs contention rate → the edge of the safe-and-profitable
  region. The batched-crypto case lets you compare *automatic* batch formation (ours +
  detector) against SSLShader's *hand-written* batching at equal correctness.
- **4.4 Soundness + optional static pre-pass** that proves independence for the safe
  classes so the detector can be *disabled* where provably unneeded (cheap fast path).

**Deliverables:** Figs detector overhead; win-vs-contention; the soundness argument.

---

## Phase 5 — Risky-class deep dives (turn ⚠️ into measured ✅/❌)
Now fully runnable locally on the Blackwell GPU (torch 2.10+cu128; vLLM installs on this stack).
- **5.1** Run **real vLLM / Triton** locally; pull the scheduler + paged-KV-cache
  allocator under the existing TSan method; determine locked vs unlocked per token step.
- **5.2** Demonstrate transparent offload end-to-end on a **stateless GPU inference**
  server (safe), and characterize precisely why dynamic-batching / LLM serving need
  the detector (hot unlocked scheduler/cache + the *semantic* batch-formation
  dependency, which is beyond a memory conflict — flag this as a named limitation).
  *Bridge to crypto:* the GPU-crypto batch queue (3.3b) and the inference batch queue
  are the **same pattern** — one mechanism, two classes.

---

## Phase 6 — Ablations, sensitivity, positioning
- **6.1 Ablations** (justify each mechanism): disable save/restore, pre-warm,
  fd-partition, lock-respect, detector → show the specific correctness failure each
  prevents.
- **6.2 Sensitivity:** #connections, offload-latency variance/jitter, conflict rate,
  lock contention.
- **6.3 Related-work comparison:** vs **SSLShader** (NSDI'11 — GPU-accelerated SSL with
  *hand-written* batching/pipelining to hide GPU latency; ours automates exactly this,
  transparently, and is the direct head-to-head for the crypto case), **State Threads /
  GNU Pth** (transparent coroutine, no offload-overlap, no correctness), **libinger**
  (transparent preemptive userspace threads), **Cimple/Psaropoulos** (coroutines for
  memory latency, needs annotation), **async-OpenSSL** (hand-written async — perf upper
  bound ours matches without code change), **Shenango/Caladan** (core reallocation,
  not intra-thread overlap). Show the open cell: transparent + correct + accelerator.
- **6.4 Portability:** nginx, lighttpd, one more stack — no per-app changes.

---

## Go/No-Go gates
1. **After Phase 1:** can we transparently intercept+yield on an unmodified app?
   No → pivot to "recompile-with-pass" framing (still a paper, weaker transparency).
2. **After 3.3:** does it beat context-switch on fine-grained offload? (Expected yes;
   this is the core value prop — if no, rethink.)
3. **After 4.3:** detector overhead < offload latency AND profitable region broad?
   Yes → top-tier (NSDI/OSDI/EuroSys/ATC). Marginal → strong workshop + safe-class paper.

## Two paper tiers
- **Minimum strong:** Phases 1, 2, 3.1–3.4 + existing measurements → "transparent,
  correct, fast on the safe classes (crypto, compression, stateless inference),
  zero code change." Solid systems paper.
- **Top-tier:** + Phase 4 (detector, soundness, profitability) + Phase 5 + Phase 6 →
  "principled transparent offload across the full app-class spectrum, with a sound
  speculate-or-serialize correctness mechanism."

## Paper skeleton (figures/tables)
1. Motivation: CPU waste of block vs busy-wait at µs offload + context-switch cost.
2. Architecture (Phase 1).
3. Conflict measurement: crypto/compression ≈0 (done).
4. Taxonomy + app-class mapping (done).
5. End-to-end latency/throughput vs baselines (3.1).
6. Offload-latency crossover (3.3) — the headline argument.
7. Detector overhead + win-vs-contention (4.2/4.3).
8. Ablations (6.1).

## What could kill it (and the mitigation)
- **No transparent offload-interception point** → recompile-with-macro framing.
- **Unlocked shared state common in target apps** → measurements already say it's rare
  for crypto/compression; detector handles the rest with safe fallback.
- **Detector overhead ≥ offload latency** → restrict to safe classes (static pre-pass),
  publish the safe-class version; still strong.
- **"Just use async APIs"** → the whole point is *unmodified* apps; 6.3 shows we match
  hand-written async without the rewrite.

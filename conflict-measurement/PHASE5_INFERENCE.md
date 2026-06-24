# Phase 5 — risky class verdict: DNN/LLM inference (measured)

## vLLM source inspection (real production system)
vLLM 0.19.0's core **scheduler** (`v1/core/sched/scheduler.py`) and **paged-KV-cache block
allocator / manager** (`v1/core/block_pool.py`, `kv_cache_manager.py`, etc.) instantiate
**ZERO `threading.Lock`/`asyncio.Lock`** — they rely entirely on the **single-threaded engine
step loop** (run-to-completion under the GIL/async event loop). This is exactly the taxonomy's
hazard: hot shared mutable state (request queues, KV-block free-list) protected only by
single-threaded execution.

## Consequence under interleaving (`inference_skel.c`, faithful skeleton + TSan)
Modeling the two hot structures (KV-block free-list allocator + dynamic batch queue) and
interleaving request handlers at the offload point (what transparent coroutines introduce):

| variant | TSan races | double-allocated KV blocks | integrity |
|---|---|---|---|
| **unlocked (vLLM-style)** | **10** | **46,178** | free-list corrupted; **SEGFAULT** in plain run |
| **locked** | **0** | 0 | 1.6M handled, 0 leaks |

A double-allocated KV block = the same KV-cache memory handed to two sequences → silent
attention corruption in a real server. The unlocked free-list race even **crashes** (OOB).

## Verdict
The taxonomy's ⚠️ for DNN/LLM dynamic/continuous batching is now **measured ❌ without
protection**: transparent coroutine offload on this class is unsafe unless the shared
scheduler/allocator is locked (the lock-respecting rule) or guarded by the speculate-or-
serialize detector. This is the class where the Phase-4 machinery is mandatory — confirming
the taxonomy on a real system. (It is also why these frameworks are single-threaded/async by
design.) Note: beyond memory safety, batching has a *semantic* order dependency (which
requests form a batch) that locking alone does not address — flagged as a class limitation.

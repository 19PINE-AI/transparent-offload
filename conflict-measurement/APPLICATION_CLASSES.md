# Transparent offload — follow-up experiments + application-class analysis

Builds on `RESULTS.md` (the crypto-path measurement). Two new experiments and a
taxonomy that predicts which application classes are amenable.

## 1. The cooperative-coroutine hazard the thread method can't see (`coop_err.c`)

TSan models each connection as its own OS thread, so thread-local storage looks
private. Under *real* coroutines (one OS thread, yield at the offload), TLS is
**shared** across coroutines. Measured directly with a ucontext scheduler and
two coroutines stamping/checking `errno` + the OpenSSL error queue across the
offload yield:

| mode | errno corruptions | err-queue corruptions |
|---|---|---|
| **naive** (no save/restore at yield) | **399,999 / 400,000** | **399,999 / 400,000** |
| **saved** (save/restore at yield) | **0** | **0** |

So it is not a rare race — per-thread state carried across an offload yield is
**structurally** clobbered (~100%). The fix is mandatory and cheap: the runtime
saves/restores `errno` + the ERR queue (and any per-thread DRBG) at each switch —
exactly what OpenSSL's own `ASYNC_JOB`/`SSL_MODE_ASYNC` does when it pauses a
handshake at the crypto offload. **Conclusion:** per-thread-state save/restore is
a required component of a transparent-offload runtime, not an optimization.

## 2. Compression class (`zlib_harness.c`, TSan-instrumented zlib 1.3.1)

Same method as crypto. Offloaded routine = `deflate()`.

| mode | races | note |
|---|---|---|
| pure (independent per-conn streams) | **0** | zlib has no cross-stream state |
| shareddict (shared read-only preset dict) | **0** | shared *reads* are safe |
| stats_lock (shared aggregate, mutex) | **0** | `count=48000` correct |
| stats_nolock (shared aggregate, no lock) | **3** | `count=47980` → **20 lost updates** |

Identical shape to bulk crypto: the **library** offload contributes zero
cross-connection conflicts; conflicts arise only from **app-level unlocked shared
state** in post-processing — and that is safe under locking or fd-partitioning.

## 3. Offload pattern taxonomy (`pattern_taxonomy.c`, TSan)

The offloaded routine runs on the accelerator, so all cross-connection conflicts
live in pre/post-processing shared state. Enumerating the shared-state *patterns*
real servers use (16 threads, 20k events, 3 trials each):

| pattern | races | safe? |
|---|---|---|
| stateless (independent request) | 0,0,0 | ✅ |
| perconn (per-connection state only) | 0,0,0 | ✅ (fd-partition) |
| ro_shared (read-only shared blob = weights/dict/LUT) | 0,0,0 | ✅ |
| counter_lock | 0,0,0 | ✅ |
| **counter_nolock** | 1,1,1 | ❌ lost updates |
| queue_lock (batch queue, mutex) | 0,0,0 | ✅ |
| **queue_nolock** | 2,2,2 | ❌ |
| cache_lock (LRU cache/allocator, mutex) | 0,0,0 | ✅ |
| **cache_nolock** | 2,2,2 | ❌ torn entries |

**The dividing line is not the application domain — it is whether the shared
mutable state is read-only, per-connection, or lock-protected.** The three
hazard rows are exactly the structures a *single-threaded event loop* keeps
**unlocked** because it relies on run-to-completion atomicity — precisely the
atomicity transparent coroutines break.

## 4. Mapping application classes onto the taxonomy

| Application class | Offloaded routine | Dominant cross-conn state | Pattern | Verdict |
|---|---|---|---|---|
| **TLS termination** (nginx/lighttpd+OpenSSL) | AES-GCM, RSA/ECDHE | seqno (per-conn); session cache (locked); alg-fetch cache (lazy) | perconn + cache_lock + lazy-init | ✅ measured ≈0; pre-warm the lazy cache |
| **Compression** (HTTP gzip, storage, backup/dedup) | deflate/zstd | z_stream (per-conn); preset dict (RO) | perconn + ro_shared | ✅ measured ≈0 |
| **Hashing / erasure coding / encryption-at-rest** | SHA/EC/AES | per-object buffers; (dedup index) | perconn (+ cache for dedup) | ✅ per-object; dedup index must be locked |
| **DNN inference — stateless** (vision, embeddings, single-shot) | GEMM/conv | model weights (RO); per-req tensors | ro_shared + perconn | ✅ like crypto |
| **Image / video** (resize, transcode, filters) | GPU/FPGA op | per-req image; LUTs/config (RO); buffer pool | perconn + ro_shared (+ pool lock) | ✅ |
| **Network functions** (IPsec/TLS gw, DPI/regex, dedup) | QAT/Hyperscan | per-flow state; flow table | perconn / per-core shard | ✅ per-flow = fd-partition; per-core sharding is a perfect fit |
| **DNN inference — dynamic batching** (Triton, TF-Serving) | batched GEMM | **batch queue + scheduler** | queue | ⚠️ safe **iff** queue is locked; also a *semantic* batch-formation dependency, not just memory |
| **LLM serving — continuous batching** (vLLM, TRT-LLM, SGLang) | attention/FFN per token | **paged KV-cache allocator, scheduler, prefix cache** (touched every token) | cache + queue, heavy | ❌ hostile unless all locked; these frameworks are *already* async/coroutine-structured for this reason |
| **OLTP/KV-store with offloaded ops** | compress/crypto/checksum | buffer pool, lock mgr, indexes | mostly lock_* | ✅ where locked (most of a DB); lock-free fast-paths are the risk |

### The predictive rule

Transparent offload is **safe with near-zero fallbacks** when a class's
cross-connection state is (a) read-only shared, (b) per-connection/per-flow, or
(c) lock-protected. It is **hazardous** when the class keeps *unlocked* shared
mutable aggregates — which happens specifically in single-threaded
run-to-completion servers (naive metrics, unlocked batch queue, unlocked
cache/KV-allocator/dedup-index).

Crypto, compression, hashing/EC, stateless inference, and image/storage ops land
on the safe side because **the heavy routine is pure** and their shared state is
read-only or already locked. Dynamic-batching and LLM serving land on the risky
side because they have *hot, unlocked* shared scheduler/cache state — unless the
framework already locks it (databases, production batchers do) or is already
async (vLLM), in which case "transparent" retrofit is moot.

### What this says for the paper

- **Lead targets (clean ≈0-conflict story):** TLS crypto + compression, extended
  to hashing/EC and stateless inference and image ops — all "stream of
  independent requests → one pure heavy routine → thin post-processing."
- **Two mandatory, cheap runtime mechanisms** the experiments justify:
  (i) save/restore per-thread state (errno/ERR/DRBG) at yield;
  (ii) pre-warm lazy global caches before serving.
- **Where the hard research is:** the queue/cache hazard patterns — i.e. stateful
  aggregating serving (batching, KV-cache). The taxonomy localizes exactly which
  structures a conflict detector / speculate-or-serialize fallback must guard,
  so the detector can be narrow instead of whole-program.

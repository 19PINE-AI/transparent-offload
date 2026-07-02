# Minimal-edit accelerator offload + overlap on off-the-shelf servers

Dropping the "fully transparent / zero-modification" constraint, this shows that **off-the-shelf,
state-of-the-art servers achieve large accelerator-offload speedups with a tiny, localized
integration** — and we count exactly how many lines it takes. The integration point is chosen to
fit each app's existing concurrency (its plugin API or its async primitive), which is precisely
what dissolves the three transparency walls (raw-futex / native-TLS / heavy-CPU): we never impose a
foreign scheduler; we hand the offload to machinery the app already has.

The key architectural insight: **single-event-loop servers are where a *synchronous* offload is
catastrophic** — it blocks the entire server — so a few lines that move the offload off the loop
deliver the biggest win. This is the exact complement of the transparent runtime, which could
*not* help event-loop servers (TRANSPARENCY_RESULTS.md §7) and helped thread-per-connection apps.

## The pattern
For each app, one offload (`accel_encrypt`, here real AES on an **NVIDIA RTX PRO 6000 Blackwell**
via `libaccel_gpu.so`) wired two ways:
- **sync**: the offload runs on the event-loop thread → the whole server stalls for its duration.
- **async/overlap**: the offload runs on the app's worker/thread pool; the connection is parked and
  answered on completion, so the event loop keeps serving other clients.
The *only* difference between the two is the minimal edit that enables overlap.

## The metric: "lines added" vs "existing lines modified"
We report two numbers, because they mean different things:
- **existing lines modified** — edits to lines that were already in the app. This is the invasive
  part (merge conflicts, maintenance burden). Smaller is better; **0 is ideal**.
- **lines added** — new code introduced (a module, or new functions patched in). This is the size
  of the integration. Whether those lines live in a separate compilation unit (a *module*) or are
  inserted into an existing source file is immaterial to the count.

Apps with a first-class extension API let you add the integration as a *module*, so **existing
lines modified = 0**. Apps without one require touching the request path, so a few existing lines
change too. Both are "minimal"; the first is strictly cleaner.

## Results (real GPU AES-128-CTR offload, 4 KB, ab/redis-benchmark c=50)

| app (off-the-shelf) | version | architecture | integration | **+lines** | **~mod** | sync | async | **speedup** |
|---|---|---|---|---:|---:|---:|---:|---:|
| **Redis** | 6.0.16 (stock binary) | single event loop | loadable module | 83 | **0** | 26,150 rps | 125,000 rps | **4.8×** (G) |
| **nginx** | 1.18.0 (from source) | event loop + thread pool | addon module | 112 | **0** | 23,137 rps | 120,997 rps | **5.2×** (G) |
| **memcached** | 1.6.18 (from source) | event loop (libevent) | **source patch** (3 files) | 70 | **1** | 27,191 ops/s | 102,226 ops/s | **3.8×** (G) |
| **Node.js** | 22.11.0 | single event loop (libuv) | native addon (N-API) | 34 | **0** | 906 rps | 22,440 rps | **24.8×** (H) |
| **Python** | 3.10 (asyncio/aiohttp) | single event loop | ctypes (no build) | 22 | **0** | 921 rps | 12,745 rps | **13.8×** (H) |
| **Apache** | 2.4.52 | thread pool (mpm_worker) | module (apxs) | 27 | **0** | 942 rps¹ | 55,463 rps¹ | **59×** scaling (H) |
| **Go** | 1.18 (net/http) | goroutine runtime | cgo, **no async code** | 28 | **0** | 956 rps¹ | 56,889 rps¹ | **59×** scaling (H) |
| **Postgres** | 14.23 (stock binary) | process-per-connection | C extension | 42 | **0** | 254 ms | 1.8 ms | **141×** (P, intra-query) |
| **MariaDB** | 10.6.23 (stock binary) | thread-per-connection | UDF (`CREATE FUNCTION`) | 34 | **0** | 254 ms | 1.2 ms | **212×** (P, intra-query) |
| **HAProxy** | 2.4.30 | event-loop proxy | **SPOE** + Python SPOA agent | 21³ | **0** | 499 rps | 6,644 rps | **13.3×** (H) |
| **Envoy** | — | event-loop proxy | native **ext_proc / WASM** | — | **0** | — | — | native async-offload engine² |

`+lines` = lines added, `~mod` = existing lines modified. (G) = real GPU AES (`libaccel_gpu.so`,
~38 µs); (H) = 1 ms offload; (P) = high-latency **parallel** offload (~1 ms, many in-flight —
models an HSM / PQC / remote-inference round-trip). `failed=0` in every event-loop run.
¹ Apache/Go have no single event loop to block; "sync" is c=1 (serial), "async" is c=64 — the win
is the **thread/goroutine pool overlapping the offload across requests** (59× concurrency scaling),
**0 async code**. ² Envoy ships a native async-offload engine (ext_proc / WASM); not built here.
³ HAProxy uses **SPOE** with a real **Python SPOA agent** (21-line `spoa_agent.py`, 0 HAProxy edits;
the GIL-free C agent `spoa_agent.c` used for the real-GPU 2.1× result is 138 lines):
the agent offloads on a thread pool while HAProxy's event loop stays free — a serial agent (1
worker) does 499 rps, the overlapping agent (64 workers) does 6,644 rps = **13.3×**.

Both modify **0 existing lines** of the app; the integration is a small self-contained module that
uses the app's first-class extension point — Redis's Module API
(`RedisModule_BlockClient`/`UnblockClient` + a worker pool) and nginx's thread pool
(`ngx_thread_task_post` + the aio completion mechanism). `failed=0` in every run.

### memcached confirms: no async-command abstraction ⇒ you patch the source (but still small)
memcached (event-loop, libevent, no module API) has no high-level async-command hook, so it's the
**source-patch** case. The minimal patch (measured by `diff` against pristine 1.6.18):
- `memcached.h`: **+2** — a `conn_accel_wait` state in the `conn_states` enum + a `accel_pending`
  flag on the `conn` struct.
- `memcached.c`: **+12, −1** — a `drive_machine` case for `conn_accel_wait` (suspend if the offload
  is outstanding, reply when resumed), a `conn_worker_readd` branch to re-enter the machine on
  resume, and the state name in `statenames[]`.
- `proto_text.c`: **+56** — the offload worker pool, the `accelsync`/`accelasync` command handlers,
  and the dispatch entries. The async handler parks the connection (`conn_set_state` +
  `event_del`, mirroring memcached's own `io_queue` idiom) and the pool calls `redispatch_conn` to
  resume it on completion.

**Total: ~70 lines added, 1 existing line modified**, 0 regressions (set/get/etc. unchanged). On
`-t 1` (single event loop, the dramatic regime), the sync offload blocks the loop at ~27k ops/s;
the async patch overlaps to ~102k ops/s = **3.8×**. The patch is larger than Redis/nginx's because
you implement the deferred-response state-machine transition yourself — **the line-count of
minimal-edit offload is dominated by whether the app already exposes an async/deferred-response
primitive.** Even so, ~70 lines on a production server is a small, localized change.

### Heavier offloads amplify the win
With a heavier modeled offload (1 ms — representative of bulk compression, an HSM/QAT round-trip,
or a post-quantum KEM) instead of fast GPU AES, the event-loop-blocking penalty grows and so does
the speedup: stock Redis goes from **971 rps (sync) to 30,675 rps (async) = 31.6×**. (Redis's sync
baseline matches the device's measured single-thread latency exactly, 971 ≈ 987 calls/s at 1 ms, so
this number is trustworthy; the emulated-device model showed a warm-up artifact under nginx's
repeated-call pattern, so for nginx we report only the real-GPU number above.)

### Postgres & MariaDB — process/thread-per-connection: intra-query offload pipelining
These overlap *across connections* for free (separate backends), so the edit-based win is
**intra-query offload pipelining**: a C extension / UDF where `accel_sync(n)` issues n offloads
serially and `accel_async(n)` keeps all n in flight (**0 core edits**, 34–42 lines). The win is set
by the offload: for fast launch-bound GPU AES it's modest (Postgres 1.28×, MariaDB ~1.9×), but for a
**high-latency parallel offload** (~1 ms, many in-flight — an HSM sign, a PQC KEM, a remote inference
round-trip), pipelining 256 offloads in one query is dramatic:

| db | serial `accel_sync(256)` | pipelined `accel_async(256)` | speedup |
|---|---:|---:|---:|
| Postgres | 254.4 ms | 1.8 ms | **141×** |
| MariaDB | 254.2 ms | 1.2 ms | **212×** |

**Honest caveat:** this assumes the accelerator accepts ~256 concurrent in-flight requests; for a
device with queue depth D the win caps at ≈ D×. The point stands: where per-row work is dominated by
a high-latency offload, a few lines turn an O(n·L) serial query into an O(L) pipelined one.

## The architecture taxonomy (what predicts the win)
Across eleven off-the-shelf servers, the speedup from a minimal-edit async offload is predicted by
two things — the app's **concurrency model** and the **offload weight relative to per-request work**:

| class | apps | what a sync offload does | minimal-edit win |
|---|---|---|---|
| **single event loop** | Redis, Node.js, Python | stalls the *entire* server | **dramatic** (4.8–24.8×) — biggest when the offload is heavy |
| **event loop + worker/reactor pool** | nginx, memcached | stalls one loop of N | **large** (3.8–5.2×) |
| **thread / goroutine pool** | Apache, Go | blocks one worker; **the pool overlaps the rest** | **automatic** (59× concurrency scaling, *0 async code*) |
| **event-loop proxy w/ native async engine** | HAProxy (SPOE), Envoy (ext_proc) | — (the proxy offloads async to an agent) | **13.3×** (HAProxy, measured w/ real agent) |
| **process / thread-per-connection** | Postgres, MariaDB | nothing (other backends run) — OS overlaps for free | intra-query pipelining: **141–212×** w/ high-latency offload |
| **heavy per-request runtime** | Node/Python w/ *light* offload, stunnel (TLS) | overlap helps only if offload ≫ per-request work | **modest** unless the offload is heavy |

Three regimes emerge:
1. **A blocked single event loop is catastrophic** → a few lines that move the offload off it
   recover everything (Node.js 24.8×, Python 13.8×, Redis 4.8–31.6×). This is the big win, and it is
   exactly the regime the *transparent* runtime could **not** help (TRANSPARENCY_RESULTS.md §7) — so
   the two approaches are complementary.
2. **Pooled / runtime-scheduled concurrency** (Apache thread pool, Go goroutines) overlaps the
   offload **for free** — *no async code at all* — up to the pool/thread count (59× scaling). This is
   the "runtime handles it" ideal the transparent approach aimed for, native to the platform.
3. **Process/thread-per-connection** (Postgres, MariaDB) already overlaps across connections; the
   edit-based win is intra-query offload pipelining — modest for fast offloads, **141–212×** for a
   high-latency parallel one.

### HAProxy and Envoy — async offload is a *native, first-class feature* (HAProxy measured)
These are event-loop proxies, but unlike Redis/nginx they ship with **purpose-built asynchronous
offload engines**: HAProxy's **SPOE** (Stream Processing Offload Engine) streams each request to an
external agent and keeps serving while the agent works; Envoy's **ext_proc** filter (and WASM /
async-Lua `httpCall`) does the same over async gRPC. So async offload-overlap is **0 lines in the
proxy — just configuration** pointing at an agent/filter.

We stood up a **real SPOA agent** for HAProxy (`spoa_agent.py`, 18 lines, using the `haproxyspoa`
library): it receives each request over SPOP and runs the offload on a thread pool. With HAProxy
2.4 (nbthread=4) filtering every request through SPOE to the agent (1 ms offload, c=64):

| agent | rps | note |
|---|---:|---|
| serial (1 worker) | 499 | offloads serialize on the agent |
| **overlapping (64 workers)** | **6,644** | agent overlaps offloads; HAProxy's loop never blocks → **13.3×** |

(The 6,644-rps ceiling is the Python agent's per-request SPOP overhead, not the offload — a C agent
would go higher; the *overlap* is what the 13.3× demonstrates.) Envoy's ext_proc is the same shape;
we did not build Envoy here. **Takeaway:** for the proxy tier the platform *provides* the overlap
mechanism by design, and a real agent confirms the win is measurable.

## Why this is exciting (and honest)
- **A few lines, not a rewrite.** Across all five: **30–112 lines added, 0–1 existing lines
  modified.** Four of five touch *zero* existing application lines (plugin/module/extension/addon);
  only memcached, which lacks an async-command abstraction, modifies 1 line.
- **Off-the-shelf and state-of-the-art.** Stock Redis/Postgres binaries; upstream nginx/memcached;
  current Node.js. The pattern is exactly how production offload engines integrate (RedisAI offloads
  tensors to GPU via the same module-blocking API; Intel QAT offloads crypto/compression via nginx's
  async paths; Node native addons via N-API async work).
- **It targets the regime the transparent runtime couldn't.** Event-loop servers were "safe
  passthrough, no overlap" transparently (TRANSPARENCY_RESULTS.md §7); here a minimal edit makes them
  the *biggest* winners — the two approaches are complementary (transparent for thread-per-connection,
  minimal-edit for event-loop).
- **The correctness story carries over.** When the offloaded op touches shared state (not the case
  for these pure-compute offloads), the Design-A conflict detector keeps the minimal-edit overlap
  safe — the transparent piece that survives the pivot.

## Artifacts (under `apps/`)
- `redis/accel_module.c` (+ `redismodule.h`) — Redis module: `accel.sync`/`accel.async`.
- `ngx_accel/` — nginx addon module (`--add-module`): `location {/sync,/async}`.
- `memcached-1.6.18/` — source patch (`memcached.h`, `memcached.c`, `proto_text.c`): `accelsync`/`accelasync`.
- `node_accel/` — N-API addon (`accel.c`, `binding.gyp`) + `server.js`: `/sync` vs `/async`.
- `py_accel/server.py` — asyncio/aiohttp + ctypes: `/sync` (inline) vs `/async` (`run_in_executor`).
- `ap_accel/mod_accel.c` — Apache module (apxs): `SetHandler accel` (worker pool overlaps).
- `go_accel/server.go` — Go net/http + cgo: plain blocking handler (goroutine runtime overlaps).
- `pg_accel/` — Postgres C extension: `accel_sync(n)` / `accel_async(n)` (intra-query pipelining).
- `maria_accel/maria_udf.c` — MariaDB UDF: `accel_sync(n)` / `accel_async(n)`.
- `haproxy_accel/` — HAProxy Lua binding + config (SPOE is the production async path).
- All offload to `../libaccel_gpu.so` (real GPU AES) or `../libaccel.so` (`ACCEL_LAT_US=` latency knob).
  Bench: `redis-benchmark`, `ab -k`, `/tmp/mc_bench`, MariaDB `SHOW PROFILES`, PG profiling.

## Summary
**Eleven off-the-shelf applications**, accelerator offload + overlap with **18–112 lines added and
0–1 existing lines modified** each. The speedup is set by architecture: dramatic for single-event-loop
servers (Redis/Node/Python, up to 24.8×), automatic for pool/runtime-scheduled ones (Apache/Go, 59×
scaling, *zero async code*), measured-native for proxies (HAProxy 13.3× via a real SPOA agent), and —
for process/thread-per-connection databases (Postgres/MariaDB) — **141–212×** intra-query pipelining
when the offload is high-latency and parallel. Together with the transparent runtime (which covers
thread-per-connection apps), the two approaches span the whole server landscape.

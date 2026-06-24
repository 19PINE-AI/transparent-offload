# #3 — Transparent offload + overlap on a real third-party binary (stunnel): the boundary from the positive side

Goal: close the gap left by §1–3 of `../TRANSPARENCY_RESULTS.md` — every overlap *speedup* so far
is on our own `conn_server`/`infer_server`. #2 identified **stunnel** as the ideal stock target:
thread-per-connection, all blocking at the libc layer (fiberizable), doing real TLS crypto. We
modeled a higher-latency crypto accelerator (HSM/QAT/post-quantum class) at stunnel's TLS record
boundary and asked whether the runtime transparently overlaps it for a speedup.

## Setup
- Stock `stunnel4` 5.6 as a TLS reverse proxy (`accept 7714` → nginx backend `7713`), unmodified.
- `libtlsoffload.so` interposes `SSL_read`/`SSL_write`: real AES-NI crypto is preserved
  (correctness), and a fixed accelerator latency `L` is injected per TLS record via `accel_encrypt`
  on a scratch buffer (does not touch the real data). Under `libtransparent` the call yields the
  connection fiber (overlap); natively it blocks the thread.
- Load: `ab -k -c 16 -n 4000 https://…`. **Both** configs pinned to one core (native threads on
  core 14; runtime carrier on core 14) for a fair single-core comparison.

## Correctness: yes. Speedup: no.
Stunnel runs correctly under the runtime — **20/20 TLS requests correct, connections fiberize
(conn-fibers > 0)** — and at low concurrency the overlap path is clean (c=4: 7.4k rps, c=16: 12.6k
rps, 0 failures). But it does **not** beat native stunnel:

| config (1 core) | L=0 | L=50µs | L=100µs |
|---|---:|---:|---:|
| **native (OS threads)** | 37,549 rps | 36,141 rps | ~22,000 rps |
| **runtime (fibers, c=16)** | — | — | **12,583 rps** (0 failed) |

The runtime is **~2–3× slower** than native stunnel on the same core, and degrades/fails at high
concurrency (c=48: 1,359 failures). This is a genuine **negative result**, and the reasons map the
boundary of the technique precisely:

### 1. No idle CPU to reclaim (the overlap premise doesn't hold)
The §1/§3 win comes from reclaiming a core that would otherwise *stall* during a synchronous
offload. But stunnel is **already thread-per-connection**: the OS overlaps each connection's
offload wait against other connections' threads (and across cores) for free. Native throughput is
**flat from L=0 to L=50** (37.5k → 36.1k) — the offload is already hidden. There is no stall for the
runtime to exploit. The overlap mechanism only pays off when the unmodified app is *synchronous and
core-limited* (our `conn_server`), not when it already exposes its concurrency as threads.

### 2. Interposition overhead > overlap benefit
stunnel does substantial real per-connection CPU work (TLS record crypto + bidirectional
proxying). The runtime taxes **every** operation: interposed `recv`/`send`/`poll`, a fiber switch
at each yield, and `errno` + **OpenSSL ERR-queue save/restore on every single yield** (libcrypto is
present, so the ERR drain/refill runs each time). For a minimal handler (`conn_server`) this tax is
negligible against the offload; for stunnel's real crypto path it dominates, making the single
carrier ~2–3× slower than native OS threads on the same core.

### 3. Single-carrier serialization + the multi-fd `poll` wall
All connections' crypto is serialized through one carrier. Worse, stunnel's transfer loop
**`poll()`s two fds at once** (client + backend). The runtime's fiber-aware `poll` handles only the
single-fd case; a multi-fd `poll` from a fiber passes through to the **real blocking `poll`, which
stalls the carrier** (confirmed: under load the carrier sits in `do_poll`, `wchan=do_poll`). This
caps concurrency and causes the c=48 failures. Cooperative multi-fd `poll` (register all fds with
epoll, wake on any) is implementable but was out of scope here.

## What this establishes
stunnel is the honest **counterexample that delimits the overlap speedup from the positive side**:
- **Transparency/correctness extend to a real stock binary** (stunnel runs, TLS correct, fibers
  engage) — the symbol-interposition machinery is not tied to our own apps.
- **The *speedup* does not.** It requires the unmodified app to be a *synchronous, core-limited
  handler that stalls on the offload* (`conn_server`, `infer_server`). A production proxy that is
  already multi-threaded, does real per-request CPU work, and multiplexes fds gains nothing — the
  OS already overlaps its offload, and the runtime's per-op tax plus single-carrier serialization
  make it a net loss.

This is the precise complement to the MariaDB wall (`../MARIADB_RESULTS.md`): MariaDB bounds
*where transparency reaches* (raw futex below libc); stunnel bounds *where the speedup applies*
(synchronous core-limited handlers, not already-threaded proxies). Together with the §1–3 positive
results they triangulate the method's envelope.

## Artifacts
`../libtlsoffload.c` (TLS-record offload shim), `stunnel.conf`, `ng/` (nginx backend). The runtime,
accelerator (`../libaccel.so`), and `ab` reproduce the table above.

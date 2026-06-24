# Transparent runtime — real sockets, end-to-end (Phases 1, 1b, 2)

A real epoll + FastWake-fiber coroutine runtime over **real TCP sockets**. The application
handler `serve()` is plain **synchronous** code (`read → offload → write`); the runtime
runs many connections as fibers on ONE core and yields at both socket I/O and the offload.
Baseline `block` = one OS thread per connection, blocking I/O + blocking offload.
An integrated multi-threaded client drives load and **verifies the transform end-to-end**.

## Phase 1 — emulated offload (latency knob), real sockets (`tco_server.c`)
64 connections, steady-state, corrected (ring-buffer) latency sampling:

| L (µs) | coro rps | block rps | win | coro p99 | block p99 |
|---:|---:|---:|---:|---:|---:|
| 5  | 292,337 | 188,803 | 1.55× | 394µs | 315µs |
| 20 | 295,024 | 144,875 | 2.04× | 354µs | 322µs |
| 50 | 296,731 | 121,986 | 2.43× | 300µs | 181µs |

**coro throughput is flat in L** (hides offload latency); block degrades; **win grows with L**
(1.55→2.43×). **`bad=0` on every run** — end-to-end transform correct. This is the real-socket
confirmation of the emulated-microbench headline.

## Phase 1b — REAL GPU AES offload (`tco_server_gpu.cu`)
`co_offload` does a real GPU AES-CTR (per-conn CUDA stream + `cudaEventRecord`); the
dispatcher polls `cudaEventQuery`. block does a synchronous `cudaStreamSynchronize` per op.
4 KB requests, 64 connections (GPU was co-tenant-contended):

| mode | rps | bad | p50 |
|---|---:|---:|---:|
| **coro** | **113,474** | 0 | 538µs |
| block | 6,128 | 0 | 1348µs |

**coro 18.5× block, end-to-end AES verified (`bad=0`).** block is crippled: each connection
thread stalls in `cudaStreamSynchronize` (~ms under contention); coro overlaps all 64
connections' GPU offloads from one CPU thread. (Absolute numbers contention-inflated; the
ratio shows the CPU-and-GPU-utilization win. Uncontended this gap narrows but coro still wins
— busy/block leave the GPU idle while stalling the CPU.)

## Phase 2 — correctness at scale
- **Pipelined, order-dependent requests** (`STATEFUL=1`, pipeline depth 1/8/32): each
  connection's reply N depends on its request count, so any per-connection reordering fails
  the client check. Result: **`bad=0`** across ~340–375K req/config (coro) — fd-partition
  serializes each connection's pipelined dependent requests correctly (the "msg #2 depends on
  msg #1" case). block: `bad=0` too.
- **AddressSanitizer** on the runtime (coro, pipelined+stateful): **0 errors**, `bad=0` over
  258K requests — the runtime introduces no memory errors.

## Phase 3 — latency rigor
- **Latency–throughput Pareto with CIs** (5 reps, `sweep_C_reps.csv`, `fig_pareto_ci.png`):
  coro Pareto-dominates block on BOTH axes — at C=8 coro 341K@22.4µs p99 vs block 321K@25.2µs;
  coro holds 22.4µs (±0.0) through C=8 while block rises; coro max throughput +41%. CIs tiny.
- **Open-loop latency vs load** (Poisson arrivals, `openloop.c` with **ready-queue dispatcher**,
  `fig_openloop.png`). Primary metrics **p50 + achieved-rate** (p99 is environmental — non-isolated
  cores + co-tenant): coro **tracks the offered rate (achieved≈offered) at p50 ≈ 23µs up to ~400K**;
  **block saturates at ~170K achieved** and its p50 explodes (>250ms) beyond. coro sustains the
  full rate at low p50 to ~2.4× block's knee. (Ready-queue replaced the O(POOL) scan; p50 tails
  from the scan are gone, remaining p99 spikes are machine noise.)

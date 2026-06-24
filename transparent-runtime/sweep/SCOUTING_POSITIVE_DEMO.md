# Scouting a positive overlap demo on a stock binary — result

Goal: find an **unmodified third-party binary** that shows the transparent-offload *overlap speedup*
(not just safety), to complement the stunnel negative (`STUNNEL_OFFLOAD_RESULTS.md`). This documents
what the overlap win actually requires, the survey of locally available binaries, and the verdict.

## The exact profile the win requires
From `conn_server`/`infer_server` (the §1–3 wins) vs the stunnel/MariaDB negatives, the overlap
speedup needs **all three** properties at once:

1. **One thread *created per connection*** (so each connection becomes a fiber 1:1, and the carrier
   multiplexes *many* connections). Pooled-worker servers fail this — fiberizing a fixed worker
   pool doesn't raise concurrency beyond the pool size.
2. **Light per-connection CPU** (so the single carrier isn't serializing heavy work). stunnel fails
   this — real TLS crypto on the carrier makes interposition overhead dominate (it ran ~2–3× slower).
3. **The offload is a *polled device*** (accelerator you spin/poll on), not OS-waitable I/O. This is
   the specific source of the §1 win: the runtime polls the device **once per carrier loop for all
   fibers**, whereas native code polls *per thread*. For ordinary socket I/O the OS already wakes
   blocked threads efficiently, so there is no centralized-polling advantage to capture.

`conn_server` (`read → accel_encrypt → write`) is the faithful embodiment of all three. The question
was whether any stock binary is too.

## Survey of locally available servers
| binary | concurrency model | fits profile? | why |
|---|---|---|---|
| nginx | single-thread event loop (+aio pool) | ✗ | no per-connection thread → no fibers (safe passthrough only) |
| redis | single-thread event loop | ✗ | same |
| memcached | libevent multi-reactor | ✗ | event loop per worker, not thread-per-connection |
| **apache2 `mpm_event`** (default) | event loop + worker pool | ✗ | event-driven |
| **apache2 `mpm_worker`** | **thread pool**, listener dispatches via queue | ✗ (prop. 1) | **pooled** workers — fiberizing 32 workers gives 32-concurrency, same as native; serves correctly under the runtime but cannot exceed its pool |
| apache2 `mpm_prefork` | process per connection | ✗ | fork → separate processes, nothing to multiplex in one carrier |
| socat / rsync / sshd | fork (process) per connection | ✗ | one connection per process |
| stunnel | **thread per connection** ✓ | ✗ (props. 2,3) | per-conn threads *do* fiberize, but heavy TLS CPU + multi-fd `poll`; network offload, not a polled device → measured **loss** |
| MariaDB | thread-per-conn + InnoDB engine | ✗ | raw-futex/io_uring wall (`../MARIADB_RESULTS.md`) |

**Apache `mpm_worker` was the only thread-per-connection-in-one-process stock candidate, and it does
serve under the runtime** (verified: `GET → ok-apache` under `LD_PRELOAD`, default and `RT_POOL`).
But its **pooled** design fails property 1: connections are dispatched to a fixed worker set via a
queue, so turning the workers into fibers does not increase the number of connections multiplexed on
the carrier — concurrency stays capped at the pool size, exactly as native. No overlap win is
structurally possible, independent of CPU weight.

## Verdict
**No stock binary on this machine fits the positive-overlap profile.** Each violates at least one
required property:
- event-driven (nginx, redis, memcached, apache `mpm_event`) → no per-connection thread to fiberize;
- pooled threads (apache `mpm_worker`) → fixed concurrency, fiberizing adds nothing;
- fork-per-connection (socat, rsync, sshd, apache `mpm_prefork`) → one connection per process;
- heavy-CPU per-conn (stunnel) → carrier serializes real crypto, interposition tax dominates;
- sub-libc engine (MariaDB) → raw-futex wall.

This is itself a finding: the overlap win lives in a **narrow niche** — *per-connection-thread,
light-CPU, polled-device-offload* servers — which in the wild are mostly **bespoke C services and
managed-runtime app servers (JVM/Go thread-per-request)**, not the classic Unix daemons sampled here.
`conn_server`/`infer_server` are honest representatives of that niche, and the §1–3 GPU results
(17.3× / 11.8×) are the positive evidence; the stock-binary contribution is correctly scoped to
**transparency + safety** (nginx, stunnel, MariaDB run correctly) plus the **boundary map**
(`SWEEP_RESULTS.md` classifier; MariaDB wall; stunnel speedup-boundary).

## Recommendation for a positive stock-binary result, if pursued
Two routes were identified:
1. **A managed-runtime thread-per-request app server** (JVM / Go thread-per-request) front-ending a
   polled accelerator — the natural home of the niche. **Attempted (JVM): failed at a fundamental
   wall.** See `../jvm/JVM_RESULTS.md`: OpenJDK 17 with a classic thread-per-connection blocking
   server *does* fiberize its connection threads 1:1, but crashes under any concurrent load because
   HotSpot keeps `Thread::current()` in a native `__thread` (ELF-TLS) slot that all fibers on the
   one carrier OS thread share — and which LD_PRELOAD cannot save/restore per fiber. This is the
   **third wall** (native-TLS thread identity), and it generalizes to Go/.NET/V8.
2. **A bespoke-but-real microservice** that is literally `recv → accel → send` (e.g., a stock
   AES/gzip offload sidecar). Functionally identical to `conn_server`; the "stock" qualifier would be
   satisfied by using a third-party such service rather than our own. Not available on this host.

**Conclusion: no positive overlap demo is achievable on a stock binary here.** The niche
(per-connection-thread + light-CPU + polled-device-offload) is real but is occupied by bespoke
C services; managed runtimes that would otherwise fit are blocked by the native-TLS wall (#1). The
positive overlap evidence legitimately remains `conn_server`/`infer_server` (§1–3), now bracketed by
a three-wall boundary map (MariaDB raw-futex, stunnel no-idle-CPU, JVM native-TLS identity).

Either makes the positive case on a non-author binary; neither is available on this host, so the
honest current position is: **overlap speedup demonstrated on representative synchronous handlers
(§1–3); transparency/safety demonstrated on real stock binaries; the niche boundary mapped.**

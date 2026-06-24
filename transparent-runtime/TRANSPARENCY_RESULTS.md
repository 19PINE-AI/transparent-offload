# Full transparency on stock unmodified binaries — results

`libtransparent.so` is an LD_PRELOAD **M:N fiber runtime** that turns an UNMODIFIED
thread-per-connection binary into overlapped coroutines. It interposes only standard symbols
(`pthread_create`, `read`, `write`, and the offload call `accel_encrypt`) — zero app-specific
knowledge. A carrier thread runs the scheduler; fibers yield at socket I/O and at the offload;
per-fiber `errno` is saved/restored across yields (the Phase-4 save/restore lesson, applied).

The app (`conn_server`) is plain synchronous thread-per-connection code, compiled standalone:
`read REQ → accel_encrypt → write REQ`. The runtime knows nothing about it.

## 1. TLS/crypto offload — emulated accelerator (latency knob), same unmodified binary
Stock (thread-per-connection, 1 core) vs **same binary + `LD_PRELOAD=libtransparent.so`**, 64 conns:

| offload latency | stock rps | runtime rps | win | bad |
|---:|---:|---:|---:|---:|
| 20 µs  | 185,300 | 265,332 | 1.43× | 0 |
| 50 µs  | 183,647 | 258,023 | 1.40× | 0 |
| 100 µs | 167,330 | 257,603 | 1.54× | 0 |
| 200 µs | 132,372 | 251,946 | **1.90×** | 0 |

**Runtime throughput is flat in offload latency (hides it); stock degrades — win grows
1.43→1.90×. `bad=0` everywhere** (AES verified end-to-end). The transparent-offload thesis,
on an unmodified binary.

## 2. Compression offload — same binary, same runtime, real zlib (offload-agnostic)
`ACCEL_OP=zip` makes the accelerator do a real deflate+inflate. Same `conn_server`, same
`libtransparent.so`: **`bad=0`** (correct). Throughput ~1.05× — because the heavy zlib
saturates the single-core emulated device (device-bound regime, not CPU/latency-bound). This
is the honest boundary: overlap helps when the CPU would stall, not when the accelerator is
itself the bottleneck. (A parallel accelerator removes this — see §3.)

## 3. REAL GPU offload — same unmodified binary (`libaccel_gpu.so`, AES on Blackwell)
The accelerator is the real GPU (per-op CUDA stream + `cudaEventQuery`). 64 conns, 4 KB:

| config | rps | p50 | bad |
|---|---:|---:|---:|
| stock + GPU (synchronous) | 5,687 | 10,534 µs | 0 |
| **same binary + runtime + GPU** | **98,624** | **548 µs** | 0 |

**17.3× throughput, 19× lower p50, correctness preserved** (stock = busy-wait synchronous).
Against a **fair block baseline** (`cudaStreamSynchronize`, OS-overlaps the 64 conn threads):
**11.9× throughput, 4.2× lower p50** (71,760 vs 6,044 rps; GPU co-tenant-contended). Either way
the runtime overlaps 64 connections' GPU offloads (many streams in flight) where the stock path
serializes them on one core. This is transparent offload to a real accelerator on an unmodified
binary.

## 4. Stock nginx under the runtime — safety on a real third-party binary
Stock `/usr/sbin/nginx` run with `LD_PRELOAD=libtransparent.so`: the runtime loads in nginx's
processes; **10/10 requests correct; `ab -n 2000 -c 50` → 0 failed, 47,386 rps.** The runtime
is **safe** on a real complex binary — it passes through transparently.

## 5. Second distinct binary — DNN inference server (`infer_server`, separate source)
A DIFFERENT unmodified app: thread-per-connection with non-trivial pre/post-processing
(normalize → GPU offload → format), separate source from `conn_server`. Same runtime, real GPU:

| config | rps | bad |
|---|---:|---:|
| stock + GPU | 5,038 | 0 |
| **same binary + runtime + GPU** | **59,643** | 0 |

**11.8× throughput, `bad=0`** — the runtime is not tuned to one app; it transparently
accelerates a second, structurally different binary. Covers the DNN-inference offload class.

## The architectural boundary (honest)
The M:N runtime delivers *overlap* for **thread-per-connection** apps (each connection is an
independent thread the runtime turns into a fiber). For **single-threaded event-loop** servers
(nginx, redis), the per-connection crypto result is needed synchronously on the event-loop
stack, so transparent *overlap* would require the runtime to own the event loop — it runs them
*safely* (§4) but without the overlap speedup. Thread-pool servers (memcached) would have their
fixed worker pool collapsed onto the carrier and are out of scope. So: transparency +
correctness are demonstrated broadly; the *overlap speedup* applies to the thread-per-connection
class (§1–3), which is exactly where the offload-wait-on-the-CPU problem lives.

## Files
`libtransparent.so` (runtime), `conn_server` (unmodified app), `libaccel.so`/`libaccel_gpu.so`
(emulated/GPU accelerators), `client` (load + end-to-end verify). All in `transparent-runtime/`.

## 6. Transparent AND correct — detector/lock-respect + save/restore wired into the runtime
The runtime now handles cross-connection shared state, not just per-connection ordering.

**(a) Per-connection ordering (fd-partition):** pipelined order-dependent requests → `bad=0`
(§ Phase 2). One fiber per connection serializes that connection automatically.

**(b) Cross-connection shared state — conflict injection** (`hostile_server.c`: RMW on a global
counter spanning the offload, so two connections interleave at the yield):

| config (under runtime) | shared | total | result |
|---|---:|---:|---|
| **UNLOCKED** | 14,170 | 303,581 | **CORRUPTED** (289,411 lost — the hazard the runtime introduces) |
| **LOCKED** (fiber-aware mutex) | 31,226 | 31,226 | **CORRECT** (0 lost), no deadlock |
| **LOCKED + naive real mutex** (`RT_REALMUTEX`) | — | — | **DEADLOCK** (client times out) |

**(c) Lock-respect is a real mechanism, and necessary:** a mutex held *across* the offload
yield would deadlock the carrier with a naive real mutex (row 3). The runtime's **fiber-aware
mutex** (interpose `pthread_mutex_lock` → `trylock`-then-park-and-retry) makes the waiter yield
instead of blocking the OS thread → correct (row 2). This is the notes' "a coroutine blocked by
a lock held by another is paused until released," implemented transparently. The locked
throughput is lower (serializing the offload on the hot lock) — exactly the detector-vs-lock
tradeoff, now end-to-end in the runtime.

**(d) Per-fiber TLS save/restore:** the runtime saves/restores `errno` (and the OpenSSL ERR
queue, when libcrypto is present) at every yield — without it, all fibers on the carrier share
one errno/ERR queue (the `coop_err` hazard). errno isolation is exercised by 250K crypto
requests at `bad=0`; the ERR path uses the identical drain/re-raise logic proven in `coop_err`.

**Net:** the runtime is **transparent AND correct** — automatic per-connection ordering, plus
lock-respect for cross-connection state (with the honest caveat that *unlocked* cross-connection
state is corrupted, exactly as the taxonomy predicts; the app must lock it, or a future build
must wire the speculate-or-serialize detector with shadow-memory tracking).

## 7. Event-driven boundary — concrete evidence
Instrumented the runtime to count connection-fibers created (overlap engaging):

| binary | connection-fibers | fiber switches | overlap engages? |
|---|---:|---:|---|
| `conn_server` (thread-per-connection) | **64** | **1,155,514** | **yes** → 17.3× (§3) |
| stock `nginx` (event-driven, multi-process) | **0** | — | no → safe passthrough (§4) |

The runtime's M:N overlap engages precisely for thread-per-connection apps (the connection
threads become fibers). nginx's per-connection processing runs inside a single-threaded
event loop (no per-connection `pthread_create`), so the mechanism doesn't engage — it runs
*safely* but without overlap. (We did NOT pursue the nginx EVP→GPU crypto redirect: GCM-on-GPU
is complex, and for small TLS records GPU crypto is *slower* than CPU AES-NI and yields no
overlap on a single-threaded worker — so it would be a loss, not a demonstration.)

## 8. MariaDB case study — the edge of transparency on a production database
Stock `/usr/sbin/mariadbd` 10.6.23 (unmodified Ubuntu binary) under `LD_PRELOAD=libtransparent.so`
is the hardest target attempted: its own thread pool, a vio I/O abstraction, daemon `fork()`,
signal threads, and InnoDB's bespoke synchronization. Running it mapped both a reusable bug and
the precise architectural boundary. Each obstacle was found by symptom → gdb on the live carrier
+ `/proc/<tid>/{syscall,wchan}` + `RT_*`-gate bisection → fix → re-validate. (Full detail:
`MARIADB_RESULTS.md`.)

**(a) The headline bug — glibc condition-variable symbol versioning.** Preloading the library
SIGSEGVs `mariadbd` at startup (`tpool::thread_pool_generic::wake → pthread_cond_signal(cond=0x2)`,
a wild pointer). Root cause is *not* our logic: `pthread_cond_{signal,broadcast,wait,timedwait}`
are the glibc functions carrying **two incompatible ABIs** — `GLIBC_2.2.5` (old condvar layout)
and `GLIBC_2.3.2` (new). A plain **unversioned** interposer breaks the version-matched relocation
and MariaDB passes garbage to the real function. Bisection nails it: interposing *only*
`pthread_cond_signal` as a pure pass-through crashes MariaDB, while interposing
`pthread_mutex_lock` / `read` / `pthread_create` the identical way is harmless — because only the
cond functions are dual-versioned. **Fix:** export the interposed cond functions at their exact
version via a linker version script (`pthread_cond_signal@@GLIBC_2.3.2`, …) and resolve the reals
with `dlvsym(RTLD_NEXT, …, "GLIBC_2.3.2")`. *Any transparent runtime interposing `pthread_cond_*`
must version-match, or it silently corrupts every app built against the modern condvar ABI.* The
build now **requires** `-Wl,--version-script=transparent.map`.

**(b) Six further transparency obstacles, each fixed:**

| # | obstacle | symptom | fix |
|---|----------|---------|-----|
| 1 | dual-ABI cond versioning | SIGSEGV at startup | version script `@@GLIBC_2.3.2` + `dlvsym` |
| 2 | vio bypasses read/write | handshake never sends greeting | yielding interposers for `recv`/`send`/`poll` (+`recvfrom`/`sendto`) |
| 3 | faked `pthread_self()` | hang in `pthread_getattr_np` (futex on garbage) | `pthread_self()` returns the **real** OS thread id; fake id opt-in (`RT_FIBERSELF`) |
| 4 | fiber stack ≠ thread stack | `ERROR 1436 Thread stack overrun` | interpose `pthread_getattr_np` → report the fiber's stack region |
| 5 | daemon `fork()` drops carrier | connections fiber-ized but never run | `pthread_atfork` rebuilds epoll + carrier in the serving child |
| 6 | SCHED_FIFO carrier | fiber spins on a mutex held by a real thread (priority inversion) | carrier defaults to **SCHED_OTHER**; FIFO is `RT_RT`-gated |
| 7 | `pthread_cond_clockwait` uninterposed | fiber-ized tpool worker blocks the carrier | interpose it → cooperative park, CLOCK_MONOTONIC→REALTIME deadline conversion |

**(c) The wall — InnoDB's raw `futex` syscalls.** Under concurrency a query fiber deadlocks the
carrier; gdb shows `syscall()` (raw futex) → InnoDB internal sync → `handler::ha_rnd_next` →
`do_handle_one_connection` (a connection fiber). InnoDB does its event/latch waits with
`syscall(SYS_futex, …)` issued **directly**, below the libc interface. A library-level M:N runtime
interposes *symbols* and **cannot intercept a raw syscall** to convert the block into a yield, so
the single carrier stalls behind it. At low/no contention the futex stays on its user-space fast
path — which is exactly why sequential queries work and concurrency does not. *This is the precise,
non-fixable boundary of transparent library-level fiberization: you can fiberize a process only
up to the point where it blocks below libc.*

**(d) Result.** With fixes 1–7, MariaDB runs **correctly and safely** under the runtime:

| config | test | result |
|---|---|---|
| `RT_NOFIBER` (supported) | startup | clean — the versioning fix |
| `RT_NOFIBER` | transaction correctness | balance transfer applied, **total conserved** (Σ=2000) |
| `RT_NOFIBER` | `mysqlslap` 32 clients × 3200 q | 0.425 s, **0 errors, 0 crashes** |
| fiber mode | handshake + parse + DDL/DML/txn (low contention) | correct (42, version, Σ=12) |
| fiber mode | concurrent InnoDB query | hits the raw-futex wall (c) |

MariaDB has no accelerator offload to overlap, so the *speedup* premise doesn't apply; its value
is the case study itself — a reusable bug (a), a checklist of what transparent fiberization of a
real server actually requires (b), and the honest negative result that delimits the method (c).
This complements the thread-per-connection *successes* (§1–3, stunnel, infer_server) with the
boundary on the opposite end.

## 9. Generalizing the boundaries across six stock binaries (`sweep/`)
Three follow-up experiments turn the MariaDB anecdote into general results across redis, memcached,
nginx, stunnel, postgres, and MariaDB. Full detail: `sweep/SWEEP_RESULTS.md` (#1, #2) and
`sweep/STUNNEL_OFFLOAD_RESULTS.md` (#3).

**(a) Cond-versioning is a real but app-specific hazard (#1).** Two minimal isolation libraries
(interpose only the four classic cond functions, unversioned vs `@@GLIBC_2.3.2`) across the five
binaries with dynamic `pthread_cond` (postgres has none): the naive **unversioned interposer
crashes 1 of 5 (MariaDB, SIGSEGV at startup) and is tolerated by redis/memcached/nginx/stunnel**;
the versioned interposer is correct for all. Mechanism: MariaDB is the **only** binary importing
`pthread_cond_clockwait@GLIBC_2.34` (C++ `std::condition_variable` on a steady clock); mixing that
newer wait path with cond interposition is what desynchronizes the wait/signal pair. A transparent
runtime can't know which binary is susceptible → **versioned cond interposition is mandatory**.

**(b) A predictive transparency classifier (#2).** Per-request blocking-syscall profiles
(`strace -f -c` under load) cleanly predict the runtime's behavior via **per-request futex density +
io_uring presence**:

| class | exemplars | signature | verdict |
|---|---|---|---|
| event-driven | redis, memcached, nginx | `epoll_wait` + `read`/`recv`; futex ≈ 0 | safe passthrough, **no overlap** |
| thread-per-conn, libc-level blocking | **stunnel**, conn_server, infer_server | `poll`/`read`/`write`/`recv`/`send`; **futex ≈ 0** (24 / 400 conns) | **fiberizable** (+ overlap if offload) |
| engine, sub-libc sync | **MariaDB/InnoDB** | **futex 73% (12,966)** + `io_uring_enter` | **wall** (raw futex below libc) |

InnoDB's futex density is ~500× stunnel's — the wall is now a *measurable category*, not a surprise.

**(c) The overlap speedup's positive-side boundary — stunnel (#3).** stunnel (the fiberizable
class) runs correctly under the runtime (20/20 TLS, fibers engage) with a modeled high-latency
crypto accelerator at its TLS record boundary — but delivers **no speedup**: native thread-per-conn
stunnel already overlaps the offload across its threads (flat 37.5k→36.1k rps, L=0→50), while the
single-carrier runtime is **~2–3× slower on the same core** (12.6k rps) because (i) there is no idle
CPU to reclaim, (ii) real TLS crypto makes per-op interposition overhead (incl. OpenSSL ERR-queue
save/restore per yield) dominate, and (iii) stunnel's multi-fd `poll` passes through to a real
blocking `poll` that stalls the carrier. **The speedup needs a synchronous, core-limited handler
that stalls on the offload (§1–3) — not an already-threaded production proxy.** Together, MariaDB
(where transparency reaches) and stunnel (where the speedup applies) triangulate the method's
envelope.

**(d) The third wall — managed-runtime native-TLS thread identity (JVM, `jvm/JVM_RESULTS.md`).**
Scouting for a *positive* stock-binary overlap demo pointed at a managed thread-per-request server.
A JVM (OpenJDK 17) running a classic thread-per-connection blocking server **does** fiberize its
connection threads 1:1 (transparency reaches the JVM), but **crashes under any concurrent fiberized
load**: HotSpot reads `Thread::current()` from a native `__thread`/ELF-TLS slot accessed inline via
`%fs`, which all fibers on the single carrier OS thread share. The runtime virtualizes `errno`, the
OpenSSL ERR queue, and `pthread_getspecific` keys per fiber, but **cannot save/restore a `libjvm`
internal `__thread` variable** — so after any switch `Thread::current()` returns the wrong
`JavaThread`, and the JVM segfaults dereferencing it (`JfrObjectAllocationSample::send_event(…,
Thread*)`). This is a fundamental, generalizable wall (Go/.NET/V8 keep thread identity in TLS/a
register too): **transparent M:N fiberization is incompatible with runtimes that store thread
identity in native thread-local storage.** The envelope now has **three walls** — sub-libc
synchronization (MariaDB raw futex), already-overlapped/heavy-CPU workloads (stunnel), and native-TLS
thread identity (JVM) — and the positive overlap speedup is correctly scoped to synchronous
core-limited handlers (§1–3).

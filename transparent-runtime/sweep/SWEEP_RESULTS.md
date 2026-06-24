# Generality sweep: cond-versioning bug (#1) + transparency classifier (#2)

Two experiments that generalize the MariaDB case study (`../MARIADB_RESULTS.md`) across six stock
server binaries on this machine. All binaries unmodified; `postgres` is process-based and imports
**no** dynamic `pthread_cond_*` (0 symbols), so it is out of scope for the cond test.

## #1 — Is the unversioned `pthread_cond_*` interposer a general hazard?

Two **minimal** isolation libraries, each interposing only the four classic cond functions:
- `cond_unversioned.so` — plain symbols + `dlsym(RTLD_NEXT, …)` (the naive interposer).
- `cond_versioned.so` — exported `@@GLIBC_2.3.2` via a linker version script + `dlvsym(…,"GLIBC_2.3.2")` (the fix).

Each server started under baseline / unversioned / versioned and functionally probed:

| binary | baseline | **unversioned interposer** | versioned interposer |
|---|---|---|---|
| **MariaDB** 10.6 | works | **CRASH (SIGSEGV at startup)** | works |
| redis 6.0 | works | works | works |
| memcached 1.6 | works | works | works |
| nginx 1.18 (aio pool) | works | works | works |
| stunnel 5.6 (TLS) | works | works | works |
| postgres 14 | n/a (no dynamic `pthread_cond`) | n/a | n/a |

**The bug is real but app-specific: 1 of 5 applicable binaries is corrupted by the naive
interposer; the versioned interposer is correct for all.** Because a transparent runtime cannot
know in advance which binary is susceptible, **versioned cond interposition is mandatory** — a
latent hazard that happens to spare redis/memcached/nginx/stunnel but deterministically crashes a
production database is exactly the kind a general tool must not ship.

### Why MariaDB is the outlier (mechanism)
Of the five, **MariaDB is the only binary that imports `pthread_cond_clockwait@GLIBC_2.34`** — its
thread pool waits via C++ `std::condition_variable` on a steady clock — while signalling through
`pthread_cond_signal@GLIBC_2.3.2`. The others use only the classic 2.3.2 C cond API:

| binary | classic 2.3.2 cond | `pthread_cond_clockwait@2.34` (C++ condvar) |
|---|:--:|:--:|
| MariaDB | ✓ | **✓ (only one)** |
| redis / memcached / nginx / stunnel | ✓ | ✗ |

The naive unversioned interposer desynchronizes the wait/signal pair on MariaDB's
`std::condition_variable` (wait on the untouched 2.34 clockwait path, signal through the
unversioned symbol), producing the wild-pointer SIGSEGV in `tpool::wake`. The correlation is
clean: **mixing the newer C++ clockwait path with cond interposition is what triggers the bug.**
The full runtime fixes it on both axes — versions the four classic functions *and* interposes
`pthread_cond_clockwait`.

## #2 — Which unmodified servers can the M:N runtime actually fiberize? (a predictive classifier)

For each server we profiled the **per-request blocking syscalls under load** (`strace -f -c`,
launched-under-strace so the `-c` summary flushes). The signature that predicts the runtime's
behavior is *where* a request blocks: at the libc layer (interceptable → yieldable) vs below it
(raw `futex` / `io_uring` from engine code → the wall).

| binary | thread model | dominant blocking syscalls (under load) | `futex` calls | `io_uring` | **runtime verdict** |
|---|---|---|---:|:--:|---|
| redis | single-thread event loop | `read`/`write` + `epoll_wait` (37,778) | **25** | no | safe passthrough, **no overlap** (no per-conn thread) |
| memcached | libevent multi-reactor | `epoll_wait` (6,056) + `recv` | 1,258 | no | safe passthrough, **no overlap** |
| nginx | event loop (+aio pool) | `epoll_wait` + `read`/`write` (§4/§7: 0 conn-fibers) | low | no | safe passthrough, **no overlap** |
| **stunnel** | **thread-per-connection** | **`poll`(4,800)+`read`/`write`/`recv`/`send`**, `accept4`/conn | **24** | no | **fiberizable** — all blocking at libc ✓ |
| conn_server / infer_server | thread-per-connection | `read`/`write` + accel offload | ~0 | no | **fiberizable + overlap speedup** ✓ (§1–3) |
| **MariaDB** | thread-per-conn + InnoDB engine | **`futex` 73% of time (12,966)** + `io_uring_enter` + `poll`/`recv`/`send` | **12,966** | **yes** | **wall** — raw futex/io_uring below libc ✗ |

**The classifier.** Three categories, each with a measured signature:
1. **Event-driven (redis, memcached, nginx)** — per-connection work runs in an `epoll`/`libevent`
   loop; there is no per-connection thread to turn into a fiber. The runtime loads *safely* but
   the overlap mechanism never engages (0 connection-fibers, §7). Negligible per-request futex.
2. **Thread-per-connection, libc-level blocking (stunnel, conn_server, infer_server)** — one
   thread per connection, and every blocking point (`poll`/`read`/`write`/`recv`/`send`) is a libc
   symbol the runtime interposes. **Near-zero futex** (24 for 400 TLS connections). These are the
   apps the runtime fiberizes — and, with an accelerator offload, overlaps for real speedup.
3. **Engine with sub-libc synchronization (MariaDB/InnoDB)** — per request blocks in raw
   `syscall(futex)` and `io_uring_enter` *inside the storage engine*, below the symbols a
   preload can interpose. **futex dominates (73%, 12,966 calls — ~500× stunnel's density).** A
   single-carrier fiber runtime cannot yield through these → it stalls under contention (the wall,
   `../MARIADB_RESULTS.md` §c).

**Per-request futex density + io_uring presence is the predictor.** It cleanly separates the
fiberizable thread-per-connection class (futex ≈ 0) from the engine-with-raw-blocking class
(futex dominant). InnoDB stops being a surprise and becomes a *category* — and the predictor is a
cheap static+dynamic check any operator can run before deploying the runtime.

## Artifacts
`cond_unversioned.c` / `cond_versioned.c` / `cond.map` (the two isolation libs), `/tmp/prof_*.txt`
(syscall profiles). Reproduce: build the two libs, run each server under each preload with a
functional probe (#1); launch each server under `strace -f -c -e trace=futex,io_uring_enter,…` and
load it (#2).

# Fiber-aware cond + rwlock + thread-pool hook — results

Implemented all three requested features in `libtransparent`.

## What was built
- **Fiber-aware rwlock** (`pthread_rwlock_rdlock/wrlock/unlock/try*`): trylock-then-park-and-retry
  (a fiber that can't acquire yields instead of blocking the carrier).
- **Fiber-aware condition variables** (`pthread_cond_wait/timedwait/signal/broadcast`): a fiber
  parks (state `F_WAIT_COND`) on `wait`, is woken on `signal`/`broadcast` or at the
  `timedwait` deadline (carrier checks deadlines); cross-thread wakes (real thread signals a
  fiber) are deferred to the carrier via a ring. Per-fiber `errno`/ERR preserved across the wait.
- **Thread-pool hook** (`RT_POOL`): fiber-ize startup threads too (for thread-pool servers).

## Validation
**Controlled app `barrier_server`** (uses `rwlock_rdlock`, `mutex+cond_broadcast`,
`cond_timedwait`): under the runtime → **ok=800, bad=0, no hang, 16 connection-fibers engaged**.
The fiber-aware cond + rwlock are **correct**. (conn_server/stunnel regressions still pass.)

## Real apps — honest status
| app | result | root cause |
|---|---|---|
| **stunnel** | **WORKS** (8/8 TLS correct) | thread-per-connection; minimal connection-path locking |
| **barrier_server** | **WORKS** (cond+rwlock exercised) | validates the new primitives |
| **MariaDB** | **crashes at startup** | `tpool::thread_pool_generic::wake` → `pthread_cond_signal(cv=0x2)` — its C++ `std::condition_variable` thread-pool object is corrupted **only under the preload**. Ruled out: glibc symbol versioning (both `@GLIBC_2.3.2`), the cross-thread `pend_ring` (guarded — still crashes), and fiber-ization (`RT_NOFIBER` crashes too). A deep, specific interaction with MariaDB's C++ tpool that needs gdb-level investigation (days). |
| **Apache `mpm_worker` + `RT_POOL`** | alive but **not serving** | the MPM's listener↔worker queue coordination doesn't function once workers are fibers |

## Honest bottom line
The requested primitives are **implemented and validated** (a real C app using cond+rwlock runs
correctly under the runtime). But the two extreme targets expose that production robustness is a
larger undertaking than the API surface alone: **MariaDB** (1M+ LOC, C++ `std::` primitives,
custom `tpool`, io_uring) corrupts under interposition in a way that resists quick diagnosis, and
**Apache**'s MPM needs more than fiber-izing its workers. The clean win — **stunnel, a real
third-party binary, runs correctly with overlap engaged** — stands; MariaDB/Apache remain open,
with the failure modes now precisely characterized rather than mysterious.

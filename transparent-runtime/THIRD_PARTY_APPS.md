# Third-party applications under the runtime — the honest finding

I attempted the three recommended real binaries (stunnel, Apache mpm_worker, MariaDB).
**Result: the M:N prototype cannot run any of them** — and the reason is precise and important.

## The cause: pthread API surface
The prototype interposes only: `pthread_create`, `pthread_mutex_lock/unlock/trylock`, `pthread_detach`.
Real servers use far more, all of which assume real OS threads:

| binary | pthread surface used (beyond the prototype's) |
|---|---|
| **stunnel** | cond_signal/wait, **key_create + get/setspecific (TLS keys)**, join, self, exit, sigmask, setschedparam |
| **mariadbd / mysqld** | all of the above **+ rwlock_* , cond_timedwait/clockwait, once, kill, key_delete, getattr_np, getaffinity_np** |
| **apache2** | threads via APR (libapr) — same surface indirectly |

## What happened
- **stunnel: SEGFAULT at startup** (before any connection) — its init creates threads that use
  TLS keys / cond vars / self, which break when those "threads" are fibers on one carrier.
- **apache2: inconclusive** (my config had an unrelated error: `unixd` is built-in); the runtime
  loaded but created **0 connection-fibers** before exit.
- **mariadbd: not run** — its pthread surface (rwlocks, cond_timedwait, once, TLS keys) guarantees
  the same outcome; running it would only re-confirm the finding.

## Why this matters (the real assessment)
The blocker is **not** the architecture — thread-per-connection IS the right target, and the
*mechanism* and *correctness* (Design A) are validated on controlled apps. The blocker is the
**engineering completeness** of the runtime: to transparently fiber-ize a stock binary you must
provide fiber-aware implementations of the WHOLE pthread API a real app touches:
- `pthread_self` (per-fiber identity), `pthread_join` (wait on a fiber), `pthread_exit`
- **per-fiber TLS** (`pthread_key_create`/`get`/`setspecific`) — fibers share the OS thread's TLS
- fiber-aware `pthread_cond_*`, `pthread_rwlock_*`, `pthread_once`
- `pthread_sigmask`/`pthread_kill` handling, `pthread_attr_*` (stack size/guard)

That is a production M:N runtime (the scope of Go's runtime or a mature State-Threads/`libco`+),
not a prototype tweak — multi-week-to-months of careful work.

## Honest bottom line for the paper
This concretely confirms the "only toy apps" threat: the controlled-app overlap demos
(conn_server/infer_server) work *because* those apps use a minimal pthread surface; **stock
third-party binaries crash on the prototype.** Two honest paths:
1. **Invest in the full fiber-aware pthread layer** → then stunnel/Apache/MariaDB become real
   targets and the "transparent overlap on real servers" claim is earned. This is the work that
   turns the prototype into a system.
2. **Reframe the contribution** around what IS solid: the correctness study + taxonomy + the
   **fused transparent conflict detector (Design A)** + the mechanism validated on controlled
   apps — with stock-binary support as clearly-scoped engineering future work.

Either way, claiming real-third-party overlap today would be dishonest. The finding itself —
*the exact pthread surface that gates transparency* — is a useful contribution.

---

## UPDATE: deferred fiber-ization + fiber-local TLS — nuanced result

A targeted fix changed the picture: **defer fiber-ization until the app starts serving**
(intercept `accept()` → only connection threads become fibers; startup threads stay real),
plus **fiber-local `pthread_key`/`get`/`setspecific`** and a fiber-aware `pthread_self`.

| app | architecture | result under the runtime |
|---|---|---|
| **stunnel** | thread-per-connection | **WORKS — 8/8 TLS round-trips correct, 8 connection-fibers engaged** |
| **Apache `mpm_worker`** | thread-pool | **runs SAFELY** (ab 1000/0 failed); workers pre-created at startup → real threads → no per-connection fibers, no overlap |
| **MariaDB** | thread-per-connection | **HANGS** — connection threads use `pthread_cond_wait`/`pthread_rwlock_*`; not fiber-aware → a fiber waits for a signal from another fiber parked on the same carrier → deadlock |

### Maturity ladder (now concrete)
1. **Works today:** thread-per-connection apps whose connection path uses only create + mutex +
   blocking I/O + TLS keys (**stunnel** — a real, widely-deployed third-party binary).
2. **Safe, no overlap:** thread-pool servers (**Apache**) — workers pre-created, so the
   per-connection fiber mechanism doesn't engage (needs a different hook).
3. **Needs more runtime:** apps using `pthread_cond_*`/`pthread_rwlock_*` in the connection path
   (**MariaDB**) — require fiber-aware condition variables and rwlocks.

### Bottom line (revised)
**One real third-party binary (stunnel) now runs correctly and transparently under the runtime,
with the overlap mechanism engaged** — the "only toy apps" threat is no longer absolute. The
remaining gap is specific and enumerable: fiber-aware `cond`/`rwlock` (MariaDB-class) and a
thread-pool hook (Apache-class). Not a wall — a finite list.

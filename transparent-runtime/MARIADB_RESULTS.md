# MariaDB under the transparent M:N runtime — what it takes, and where the wall is

We ran **stock `/usr/sbin/mariadbd` 10.6.23** (Ubuntu binary, unmodified) under
`LD_PRELOAD=libtransparent.so`. MariaDB is the hardest third-party target attempted: a
production database with its own thread pool, vio abstraction layer, daemon fork, signal
threads, and InnoDB's bespoke synchronization. Getting it to run uncovered a **taxonomy of
transparency obstacles** — each a concrete, generalizable lesson for any LD_PRELOAD runtime that
interposes pthreads — and one **fundamental architectural boundary**.

Every obstacle below was found by symptom → root-caused (gdb on the live runtime, `/proc/<tid>/`
syscall/wchan inspection, binary-search ablation via `RT_*` gates) → fixed → re-validated.

## The headline bug: glibc condition-variable symbol versioning (crash at startup)

**Symptom:** `mariadbd` SIGSEGVs during startup the instant the library is preloaded —
`tpool::thread_pool_generic::wake → pthread_cond_signal(cond=0x2)` (a wild pointer).

**Root cause (generalizable):** `pthread_cond_signal/broadcast/wait/timedwait` are the glibc
functions that carry **two incompatible ABIs** — `GLIBC_2.2.5` (old condvar layout) and
`GLIBC_2.3.2` (new). MariaDB requests `pthread_cond_signal@GLIBC_2.3.2`. A naive interposer
defines a **plain, unversioned** `pthread_cond_signal`; the dynamic linker's version-matched
relocation is broken by the unversioned definition and MariaDB ends up passing a garbage `cond`
to the real function. Bisection proved it: interposing *only* `pthread_cond_signal` as a pure
pass-through crashes MariaDB, while interposing `pthread_mutex_lock` / `read` / `pthread_create`
the same way is harmless — because only the cond functions are dual-versioned.

**Fix:** export the interposed cond functions **at their exact version** via a linker version
script (`pthread_cond_signal@@GLIBC_2.3.2`, …) and resolve the real ones with
`dlvsym(RTLD_NEXT, "pthread_cond_signal", "GLIBC_2.3.2")` instead of `dlsym`. After this,
MariaDB starts cleanly. *This is the single most important lesson: any transparent runtime that
interposes `pthread_cond_*` must version-match, or it silently corrupts every app built against
the modern condvar ABI.*

## The transparency obstacle taxonomy (each fixed)

| # | Obstacle | Symptom | Fix |
|---|----------|---------|-----|
| 1 | **glibc cond symbol versioning** | SIGSEGV at startup (wild `cond` ptr) | version script `@@GLIBC_2.3.2` + `dlvsym` |
| 2 | **vio bypasses read/write** | handshake stalls; never sends greeting | MariaDB's vio uses `recv`/`send`/`poll`, not `read`/`write` → added yielding interposers for all three (+`recvfrom`/`sendto`) |
| 3 | **`pthread_self()` faked for fibers** | fiber hangs in `pthread_getattr_np` → futex on garbage | `pthread_self()` **must** return the real OS thread id; glibc internals (stack bounds, cancellation) treat it as a real `struct pthread*`. Fake per-fiber id is opt-in (`RT_FIBERSELF`) only |
| 4 | **fiber stack vs thread stack** | `ERROR 1436 Thread stack overrun: <huge> bytes used` | interpose `pthread_getattr_np` to report the **fiber's** stack region (`fw_fiber.stack_base/size`) so MariaDB's `my_get_stack_bounds` recursion guard is correct |
| 5 | **daemon `fork()` drops the carrier** | connections fiber-ized but never run (carrier lives in the parent) | `pthread_atfork` child handler rebuilds epoll + restarts the carrier in the serving child |
| 6 | **SCHED_FIFO carrier starves real lock holders** | fiber spins forever on a mutex held by a real MariaDB thread (priority inversion) | carrier runs **SCHED_OTHER** by default; `RT_RT` re-enables the dedicated-core FIFO mode (microbench only) |
| 7 | **`pthread_cond_clockwait` not interposed** | fiber-ized tpool worker blocks the carrier in `wait_for_tasks` | interpose `pthread_cond_clockwait` (glibc 2.30+; used by libstdc++ `condition_variable` and MariaDB's tpool) → park cooperatively, with CLOCK_MONOTONIC→REALTIME deadline conversion |

With fixes 1–7, **MariaDB completes the full client handshake and executes queries through
fiber-ized connections** (verified: `SELECT 6*7`→42, `VERSION()`, DDL+DML+aggregate, and a
correct multi-statement transaction with balance transfer + conserved total).

## The wall: InnoDB's raw `futex` syscalls (a fiber cannot be cooperatively yielded out of them)

Under **concurrent** load (`mysqlslap --concurrency 16/32`), a connection fiber executing a
`SELECT` deadlocks the carrier. gdb on the live carrier:

```
#0  syscall ()                                   ← raw futex, NOT through pthread_*
#1-#6 InnoDB internal sync (os_event / mutex)
#7  handler::ha_rnd_next            (table scan)
...
#17 dispatch_command  #18 do_command  #19 do_handle_one_connection   ← a connection FIBER
```

InnoDB implements its internal synchronization (event waits, rw-latches) with **`syscall(SYS_futex, …)` issued directly**, bypassing the `pthread_*` layer entirely. A userspace
M:N runtime interposes *library symbols*; it **cannot intercept a raw syscall** to convert the
block into a fiber yield. So a fiber that blocks on an InnoDB futex under contention parks the
single carrier OS thread, and every other fiber stalls behind it. (At low/no contention the
futex takes its user-space fast path and never enters the kernel — which is exactly why
single/sequential queries work and concurrency does not.)

This is **not a fixable bug** — it is the architectural boundary of transparent, library-level
M:N fiberization: *you can transparently fiberize a process only up to the point where it does
its own blocking below the libc interface.* InnoDB (like any engine using direct futexes,
io_uring waits, or inline atomics-with-futex-fallback) sits below that line.

## Supported configuration and validation

**`RT_NOFIBER=1` is the supported mode for MariaDB**: the runtime loads transparently, the
cond-versioning fix applies, all threads stay real, and InnoDB's own synchronization is
untouched. Validation (stock binary, `LD_PRELOAD=libtransparent.so`, `RT_NOFIBER=1`):

| test | result |
|---|---|
| startup | clean (no crash) — the versioning fix |
| transaction correctness | balance transfer applied, **total conserved** (700/1300, Σ=2000) |
| `mysqlslap` 16 clients × 1600 q | 0.328 s, **0 errors, 0 crashes** |
| `mysqlslap` 32 clients × 3200 q | 0.425 s, **0 errors, 0 crashes** |
| `information_schema` scan | 292 tables, correct |

**Fiber mode (default)** additionally works for the **connection/protocol layer and
low-contention queries** (handshake, auth, parse, single-statement DML/DDL/transactions) — a
genuine demonstration that the M:N machinery can drive a production DB's client path — but is
**not** appropriate for MariaDB as a whole because (a) MariaDB has no accelerator offload to
overlap (the runtime's *speedup* premise doesn't apply), and (b) InnoDB's raw-futex
synchronization hits the wall above under concurrency.

## Why this matters for the paper

MariaDB is the case study that maps the **edge of transparency**. The cond-versioning finding is
a concrete, reusable contribution (every pthread-interposing tool needs it). Obstacles 2–7 form
a checklist of what "transparently fiberize an unmodified server" actually requires beyond the
toy case. And the InnoDB raw-futex wall is the honest, precise statement of where
library-level M:N interposition must stop — complementing the earlier thread-per-connection
successes (conn_server, infer_server, stunnel) with a principled negative result.

## Artifacts
`libtransparent.c` (runtime + all seven fixes), `transparent.map` (version script — the build
now links `-Wl,--version-script=transparent.map`). Build:
`gcc -O2 -fPIC -shared -Wl,--version-script=transparent.map libtransparent.c detector.c fw_fiber.c fw_switch.S -ldl -o libtransparent.so`

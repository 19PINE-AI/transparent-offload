# JVM thread-per-request under the runtime — the third wall: native `__thread` thread identity

Follow-up to the scouting report (`../sweep/SCOUTING_POSITIVE_DEMO.md`), which named a
**managed-runtime thread-per-request server** as a candidate for the positive overlap demo. We
installed OpenJDK 17 and wrote the most favorable possible target: a classic **thread-per-connection
blocking server using platform threads** (`Server.java` — `new Thread(() -> handle(sock)).start()`
per accepted connection → one `pthread_create` per connection → 1:1 connection→fiber, light
per-request CPU). This is structurally identical to `conn_server`, so it *should* be the ideal fit.

## Transparency reaches the JVM — connection threads fiberize
The JVM runs under the runtime and its connection threads **do** fiberize:
- `java -version` / the server start cleanly under `LD_PRELOAD=libtransparent.so` (the
  cond-versioning fix applies); single requests return `ok-java` in `RT_NOFIBER`, default, and
  `RT_POOL` modes.
- Instrumented run: `accept serving` fires (the JVM's `ServerSocket.accept()` goes through the
  interposed `accept`), then `fiberize conn 1/2/3…` — **each `new Thread()` per connection becomes a
  fiber**. The M:N machinery engages on a real JVM, not just C servers.

## But it crashes under *any* concurrent fiberized load — and the reason is fundamental
| config | result under concurrent load (`ab -c 4…16`) |
|---|---|
| default (tiered JIT) | **SIGSEGV** — `CompileBroker::init_compiler_runtime` `guarantee(comp != NULL)` |
| `-Xint -XX:+UseSerialGC` | **SIGSEGV** — `JfrObjectAllocationSample::send_event(…, Thread*)+0x21` |
| `-Xint -XX:-FlightRecorder` (c=4) | **SIGSEGV** — same `…send_event(…, Thread*)` |

Two distinct failure mechanisms, both rooted in **OS-thread identity that LD_PRELOAD cannot
virtualize per fiber**:

1. **Lazily-created internal JVM threads get wrongly fiberized.** HotSpot spins up JIT compiler
   threads (and, with other collectors, GC threads) *on demand during execution* — i.e. after
   `g_serving` is set. The runtime fiberizes them like connection threads, and a `CompilerThread`
   running as a fiber fails its own init (`comp != NULL`). (Same class as MariaDB's tpool workers.)

2. **The deep wall — `Thread::current()` lives in a native `__thread` slot.** HotSpot reads the
   current `JavaThread` from a compiler `__thread` variable (ELF TLS, accessed directly via `%fs`
   by JVM code). Every fiber on the one carrier OS thread shares that single slot. The runtime
   saves/restores `errno` and the OpenSSL ERR queue across a fiber switch, but it **cannot** save/
   restore HotSpot's `__thread` thread pointer (its address is internal to `libjvm.so`, version-
   specific, and `%fs`-relative reads are not interposable). So after any switch where a *different*
   fiber ran, `Thread::current()` returns the **wrong/stale `JavaThread`**. The crash signature is
   exact: `JfrObjectAllocationSample::send_event(…, Thread* current)` segfaults dereferencing the
   bad `Thread*` — a connection fiber allocates an object, HotSpot fetches the current thread to
   sample it, and gets garbage. The same corruption underlies safepoint state, TLAB pointers,
   thread-local handle areas, and GC barriers — disabling one feature (JFR) just moves the crash to
   the next `__thread` consumer.

## Why this is a real, generalizable boundary
The runtime virtualizes per-fiber state only for what it can name and interpose: `errno`, the
OpenSSL ERR queue, and `pthread_getspecific` keys. **Native `__thread`/ELF-TLS variables are
invisible to it** — they are read inline via the thread pointer register, with no library call to
hook. Any runtime that stores *thread identity* (not just incidental state) in `__thread` or a
dedicated register therefore cannot be transparently M:N-fiberized by symbol interposition:
- **HotSpot/JVM** — `Thread::current()` in `__thread` (shown here).
- **Go** — the `g`/`m` pointer in a register (and Go already has its own M:N scheduler).
- **.NET / V8 / TCMalloc** — thread-local context / per-thread caches in `__thread`.

This is the third, distinct wall in the envelope:
- **MariaDB** — raw `syscall(futex)` below libc (transparency reach for *synchronization*).
- **stunnel** — no idle CPU + interposition tax (*speedup applicability*).
- **JVM** — native `__thread` thread *identity* can't be virtualized per fiber (*managed-runtime
  fiberization*).

## Verdict on the positive demo
The JVM was the most promising remaining route, and it fails for a fundamental reason, not a
fixable bug: **transparent M:N fiberization is incompatible with managed runtimes that keep thread
identity in native thread-local storage.** Making it work would require JVM-version-specific
knowledge (locating and context-switching HotSpot's `__thread` thread pointer and TLAB/handle
state) — which is exactly the *non-transparent* coupling the approach is designed to avoid.

Net: the honest position is unchanged and now better-bounded. The overlap *speedup* is demonstrated
on synchronous handlers that satisfy all three niche properties (`conn_server`/`infer_server`, §1–3:
17.3× / 11.8×). On real stock binaries the contribution is **transparency + safety** (nginx,
stunnel, Apache `mpm_worker`, MariaDB all run correctly) plus a **three-wall boundary map** that
says precisely where — and why — the technique stops: sub-libc synchronization (MariaDB),
already-overlapped/heavy-CPU workloads (stunnel), and native-TLS thread identity (JVM).

## Artifacts
`Server.java` (thread-per-connection blocking server). Reproduce a crash:
`LD_PRELOAD=…/libtransparent.so RT_CORE=14 java -Xint -XX:+UseSerialGC Server <port> 0`, then
`ab -c 4 -n 800 http://127.0.0.1:<port>/`.

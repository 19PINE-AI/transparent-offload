# Conflict-frequency measurement — TLS crypto path (transparent offload)

**Question.** If a transparent-coroutine runtime parks a TLS connection's event
handler at its crypto "offload" call and runs *another* connection's handler
meanwhile, how often do the two interleaved handlers conflict on **unlocked,
cross-connection shared state** — the silent conflicts fd-partitioning + lock
respecting do **not** catch?

## Method

A "conflict" in the coroutine scheme == a **data race** when two *different*
connections' server-side handlers run concurrently, sharing one `SSL_CTX`, with
no synchronization — **excluding** anything OpenSSL already protects with a lock
(the scheme respects locks). ThreadSanitizer reports exactly this set and
automatically ignores per-connection/stack state and lock-protected state.

- OpenSSL 3.0.2 rebuilt from source with `-fsanitize=thread no-asm` (pure C so
  TSan sees every memory op inside libssl/libcrypto), static, thread-aware.
- `harness.c`: one shared server `SSL_CTX` (like an nginx worker), many in-memory
  TLS connections (BIO pairs, no sockets). Each worker thread drives its **own**
  connections; threads share only the `SSL_CTX` + OpenSSL globals. Every race
  reported is therefore genuinely cross-connection.
- Offload boundaries measured:
  - **data** — established conns, repeated record exchanges = symmetric bulk
    AES-GCM en/decrypt + per-conn seqno + post-processing (QAT/GPU bulk offload).
  - **handshake** (TLS1.3, default stateless tickets) and **handshake12**
    (TLS1.2, `SSL_OP_NO_TICKET`, 64-entry cache → forces the **stateful internal
    session cache** with insert+evict churn) = asymmetric offload + session cache.
- **Positive control** (`poscontrol`): post-processing bumps an unlocked shared
  counter. TSan reports **exactly 1** race on it → proves the harness really
  overlaps handlers in a conflict-exposing way (guards against false zeros).

## Results

| Path | Config | Steady-state ops measured | Cross-conn conflicts |
|---|---|---|---|
| Positive control | 4×200 | — | **1** (planted, as expected) |
| **Data (bulk AES-GCM)** | 16×4000, ×runs | >100k record exchanges | **0** |
| **Handshake TLS1.3** | 16×800 (pre-warmed) | ~13k handshakes | **0** |
| **Handshake TLS1.2 + session cache** | 16×150, **pre-warmed**, 12 trials | ~29k handshakes | **0 / 12 trials** |
| Handshake TLS1.2 + session cache | 16×150, **cold start**, 12 trials | — | races in **6 / 12 trials**, ≤19/run |

**The only conflicts found anywhere** are transient, timing-dependent
**lazy-initialization races in OpenSSL 3.0's global algorithm-fetch cache**
(the provider/property `OSSL_METHOD_STORE`):

```
  crypto/property/property.c   alg_do_one / alg_do_each
  crypto/sparse_array.c        sa_doall / ossl_sa_set
  crypto/stack/stack.c         OPENSSL_sk_value / OPENSSL_sk_num
```

These fire only while the shared method store is being populated on first use,
in ~50% of cold starts, and are **100% eliminated by a 3-handshake
single-threaded pre-warm** (0/12 warm trials).

**The session cache was NOT exercised by this BIO-pair test** (`SSL_CTX_sess_number` stayed
**0** — the in-memory handshake creates resumable sessions and fires the new-session callback,
but the internal LHASH store never populates without real resumption). So the handshake-path
races above are all in the provider/property method store; the session cache itself was
untested here. **Resolved separately over real sockets — see below.**

### Session cache, properly tested over REAL sockets (`sess_socket_harness.c`)
The gap above is closed with a real-TCP-socket, multi-threaded harness against the
TSan-instrumented OpenSSL, configured exactly like nginx (`ssl_session_cache builtin;
ssl_session_tickets off; ssl_protocols TLSv1.2`). N server threads share one `SSL_CTX`;
its **internal cache is genuinely populated and exercised** this time:

| mode | concurrent exercise | sess_number | TSan races in OpenSSL |
|---|---|---|---|
| read-heavy (resumption) | 5,141 cache-hit lookups | 6 | **0** |
| write-heavy (constant add) | 448 concurrent adds | 448 | **0** |
| write + forced eviction (cache=128) | 1,384 adds w/ eviction churn, 8 threads | 128 (full) | **0** |

(The only race TSan first flagged was my harness's own `volatile stop` flag — fixed with an
atomic; OpenSSL itself: zero.) **Real nginx cross-check:** nginx serving HTTPS with the same
config shows `openssl s_client -reconnect` reporting **`Reused`** with a stable Session-ID —
the internal cache is populated and read in the real server over real sockets.

**Conclusion (now evidence-backed, not inferred):** OpenSSL's internal session cache — the
read (lookup), write (add), and evict (LRU) paths — is **race-free under concurrent
cross-connection access**, i.e. properly lock-protected, so it is **safe under the
lock-respecting transparent-coroutine scheme**. This is the claim the earlier audit had
correctly flagged as unsupported; it is now supported.

## Interpretation

For the OpenSSL crypto path the favorable hypothesis **holds**:

1. **Steady-state cross-connection conflict frequency ≈ 0** on both the bulk
   symmetric path and the handshake path. The offloaded routine is pure; its
   post-processing touches per-connection state or lock-protected globals.
2. The lone conflict source is **eliminable warm-up** of a lazy global cache —
   fix is a known one-liner (prime the cache before serving).
3. So a transparent-coroutine offload scheme that (a) partitions by fd and
   (b) respects OpenSSL's existing locks needs **near-zero fallbacks** on this
   workload. This is the empirical green light for the crypto target.

## Caveat — the one hazard this method cannot see (and it matters)

TSan's thread model treats **thread-local storage as private**. But under real
coroutines, all coroutines on one OS thread **share** that thread's TLS. So
OpenSSL's per-thread **error queue** (and per-thread DRBG) flip from "safe
(per-thread)" to "shared across coroutines" — a real hazard invisible here:
coroutine A leaves pending error state, yields at offload; coroutine B runs
`ERR_clear_error()` (done at the top of `SSL_read`/`SSL_write`); A resumes with
a corrupted error queue.

This is **known and already solved**: a coroutine runtime must save/restore the
per-thread OpenSSL state at each yield — which is exactly what OpenSSL's own
`ASYNC_JOB` / `SSL_MODE_ASYNC` machinery does when it pauses a handshake at the
crypto offload. The transparent scheme generalizes that. A follow-up
single-thread cooperative experiment should quantify error-queue conflicts
directly.

## Reproduce

```bash
cd conflict-measurement
# build TSan OpenSSL (already in openssl-3.0.2/), then:
gcc -g -O1 -fno-omit-frame-pointer -fsanitize=thread harness.c \
  -I openssl-3.0.2/include openssl-3.0.2/libssl.a openssl-3.0.2/libcrypto.a \
  -lpthread -ldl -o harness
export TSAN_OPTIONS="halt_on_error=0 history_size=7"
./harness poscontrol 4 200          # methodology check -> 1 race
./harness data       16 4000 1      # bulk path        -> 0
./harness handshake12 16 150 0      # cold handshake   -> sometimes warm-up races
./harness handshake12 16 150 1      # pre-warmed       -> 0
```
Note: requires `sudo sysctl -w vm.mmap_rnd_bits=28` for the TSan runtime on 6.8 kernels.

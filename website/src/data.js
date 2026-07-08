// All numbers are the paper's real measurements (paper/figs/gen_figs.py, body.tex).

export const COLORS = {
  ink: '#1A1A2E',
  slate: '#34507F',   // single event loop
  teal: '#2A9D8F',    // event loop + pool
  green: '#4E8B3B',   // thread / goroutine pool
  amber: '#E08A1E',   // per-connection DB
  purple: '#7B5EA7',  // proxy
  gray: '#9AA3B2',
  red: '#B0241B',
  light: '#EAEDF3',
}

export const MODELS = {
  loop:  { label: 'single event loop',        color: COLORS.slate },
  pool:  { label: 'event loop + pool',        color: COLORS.teal },
  tpool: { label: 'thread / goroutine pool',  color: COLORS.green },
  db:    { label: 'per-connection DB',        color: COLORS.amber },
  proxy: { label: 'proxy (offload agent)',    color: COLORS.purple },
}

// Headline: real GPU, 1 MiB AES, speedup over synchronous offload.
// `dagger`: the databases pipeline the op intra-query
// (transparent-runtime/apps/db_intraquery_gpu.csv).
export const HEADLINE = [
  { app: 'Apache',    speedup: 3.45, model: 'tpool', lines: 27 },
  { app: 'Redis',     speedup: 3.01, model: 'loop',  lines: 83 },
  { app: 'Go',        speedup: 3.01, model: 'tpool', lines: 28 },
  { app: 'memcached', speedup: 2.93, model: 'pool',  lines: 70 },
  { app: 'nginx',     speedup: 2.74, model: 'pool',  lines: 112 },
  { app: 'MariaDB',   speedup: 2.59, model: 'db',    lines: 34, dagger: true },
  { app: 'Postgres',  speedup: 2.59, model: 'db',    lines: 42, dagger: true },
  { app: 'Node.js',   speedup: 2.54, model: 'loop',  lines: 34 },
  { app: 'Python',    speedup: 2.37, model: 'loop',  lines: 22 },
  { app: 'HAProxy',   speedup: 2.10, model: 'proxy', lines: 138 },
]

// Spectrum scatter: lines added vs speedup (log-log). Transparent runtime at ~0 lines.
export const SPECTRUM = HEADLINE.map(d => ({ ...d, x: d.lines, y: d.speedup }))
export const TRANSPARENT_POINT = { app: 'transparent runtime', x: 1.4, y: 17.3, lines: 0 }

// AES block-size sweep (idle real GPU, Python asyncio single event loop, 50 clients).
export const BLOCKSIZE = [
  { kb: 4,    label: '4K',   latencyUs: 46.4,   sync: 3750, async: 4667, speedup: 1.24 },
  { kb: 8,    label: '8K',   latencyUs: 47.0,   sync: 3688, async: 4650, speedup: 1.26 },
  { kb: 16,   label: '16K',  latencyUs: 51.4,   sync: 3657, async: 4573, speedup: 1.25 },
  { kb: 32,   label: '32K',  latencyUs: 60.3,   sync: 3501, async: 4371, speedup: 1.25 },
  { kb: 64,   label: '64K',  latencyUs: 70.2,   sync: 3326, async: 4355, speedup: 1.31 },
  { kb: 128,  label: '128K', latencyUs: 92.7,   sync: 2970, async: 4218, speedup: 1.42 },
  { kb: 256,  label: '256K', latencyUs: 126.5,  sync: 2706, async: 4128, speedup: 1.53 },
  { kb: 512,  label: '512K', latencyUs: 191.8,  sync: 2454, async: 4494, speedup: 1.83 },
  { kb: 1024, label: '1M',   latencyUs: 361.6,  sync: 1538, async: 3644, speedup: 2.37 },
  { kb: 2048, label: '2M',   latencyUs: 653.6,  sync: 966,  async: 2858, speedup: 2.96 },
  { kb: 4096, label: '4M',   latencyUs: 1188.1, sync: 506,  async: 1937, speedup: 3.83 },
  { kb: 8192, label: '8M',   latencyUs: 2258.6, sync: 227,  async: 1228, speedup: 5.41 },
]

// Open-loop latency study (Poisson arrivals, controlled 20 µs emulated offload).
export const LATENCY_OVERLAP = [
  { off: 51.4,  p50: 22.8, p99: 4243.4 },
  { off: 102.7, p50: 22.9, p99: 19778.4 },
  { off: 205.4, p50: 23.3, p99: 19660.3 },
  { off: 308.3, p50: 24.6, p99: 30084.2 },
  { off: 410.7, p50: 32.2, p99: 43123.4 },
  { off: 462.3, p50: 86919.4, p99: 97054.8 },
]
export const LATENCY_BLOCK = [
  { off: 50.7,  p50: 25.4, p99: 11649.2 },
  { off: 102.9, p50: 27.7, p99: 25898.4 },
  { off: 205.4, p50: 259500.0, p99: 325811.6 },
  { off: 216.7, p50: 337506.9, p99: 388513.3 },
  { off: 250.4, p50: 336862.1, p99: 379708.5 },
  { off: 275.9, p50: 186429.1, p99: 385242.2 },
]

// Correctness (stock Redis, real GPU offload).
export const LOST_UPDATES = [
  { name: 'naive overlap', lost: 23450, color: COLORS.red },
  { name: 'detector + enforce', lost: 0, color: COLORS.green },
]
export const CORRECTNESS_TPUT = [
  { name: 'high contention', naive: 24.06, detector: 24.69, lock: 13.81 },
  { name: 'low contention',  naive: 22.78, detector: 23.94, lock: 14.24 },
]

// Fiberizability classifier: per-request futex calls.
export const CLASSIFIER = [
  { app: 'Redis',            futex: 25,    kind: 'event-driven',  color: COLORS.gray },
  { app: 'stunnel',          futex: 24,    kind: 'fiberizable',   color: COLORS.teal },
  { app: 'memcached',        futex: 1258,  kind: 'event-driven',  color: COLORS.gray },
  { app: 'MariaDB (InnoDB)', futex: 12966, kind: 'sub-libc wall', color: COLORS.red },
]

// Concurrency-model regimes (Prediction 1). `primitive` = where the suspend/resume machinery lives.
export const REGIMES = [
  {
    title: 'Single event loop', color: COLORS.slate, examples: 'Redis · Node.js · Python',
    cost: 'A synchronous offload stalls every connection at once — a pathology of structure, not capacity.',
    win: '2.4–3.0×', note: 'dramatic: the reroute restores full concurrency', primitive: 'the deferred reply',
  },
  {
    title: 'Event loop + pool', color: COLORS.teal, examples: 'nginx · memcached',
    cost: 'A synchronous offload stalls one loop of several.',
    win: '2.7–2.9×', note: 'reroute through the existing worker pool', primitive: 'the worker-pool queue',
  },
  {
    title: 'Thread / goroutine pool', color: COLORS.green, examples: 'Apache · Go',
    cost: 'Parking the worker is the routing — the pool overlaps the rest automatically.',
    win: '3.0–3.5×', note: 'automatic: zero asynchronous code needed', primitive: 'the pooled worker itself',
  },
  {
    title: 'Proxy + agent', color: COLORS.purple, examples: 'HAProxy',
    cost: 'The reroute is configuration plus an external offload agent.',
    win: '2.1×', note: 'proxy changes config only; a standalone 138-line C agent carries the offload', primitive: 'the SPOE offload engine',
  },
  {
    title: 'Per-connection DB', color: COLORS.amber, examples: 'PostgreSQL · MariaDB',
    cost: 'The OS already suspends and resumes whole backends — overlap across connections is free.',
    win: '2.6×', note: 'only intra-query pipelining of a serial offload loop remains', primitive: 'the OS scheduler',
  },
]

// The interposition envelope: three ways behavior escapes the libc symbol layer.
export const WALLS = [
  {
    n: 1, escape: 'BELOW THE LAYER', title: 'Raw syscalls under libc',
    what: 'InnoDB synchronizes with raw futex system calls issued directly, bypassing pthread. A preloaded library interposes symbols, not syscalls — a fiber blocking in a raw futex stalls the whole carrier, and under contention the server deadlocks.',
    evidence: 'Confirmed by a live backtrace of the frozen carrier inside InnoDB’s synchronization code.',
  },
  {
    n: 2, escape: 'BESIDE THE LAYER', title: 'Thread identity in native TLS',
    what: 'The JVM keeps “the current thread” in a native thread-local slot read inline from a CPU register — no library call to interpose. Fibers sharing one carrier cannot get distinct values of that slot, so after a switch the runtime reads the wrong thread object and segfaults.',
    evidence: 'Observed the moment two fiberized JVM requests interleave; same mechanism in Go and .NET.',
  },
  {
    n: 3, escape: 'BEHIND THE LAYER', title: 'No idle CPU to reclaim',
    what: 'Overlap needs idle CPU during the offload. A TLS terminator burning real CPU on per-request crypto has none: the OS already overlaps its offloads across threads, while the single carrier serializes the crypto and pays an interposition tax on every yield.',
    evidence: 'Ends up 2–3× slower than native on the same core.',
  },
]

// Correctness taxonomy: shared-state patterns under overlapped execution,
// measured in race-instrumented harnesses spanning crypto (OpenSSL) and compression (zlib).
export const PATTERNS = [
  {
    title: 'Read-only shared state', safe: true,
    examples: 'model weights · dictionaries · lookup tables',
    why: 'Never written, so no interleaving can corrupt it.',
    result: '0 conflicts measured',
  },
  {
    title: 'Per-connection state', safe: true,
    examples: 'session buffers · parser state',
    why: 'One fiber per connection serializes a connection’s dependent requests for free.',
    result: '0 conflicts measured',
  },
  {
    title: 'Lock-protected state', safe: true,
    examples: 'mutex-guarded maps and queues',
    why: 'Locks serialize access regardless of how handlers are scheduled.',
    result: '0 conflicts measured',
  },
  {
    title: 'Unlocked shared mutable aggregates', safe: false,
    examples: 'counters · batch queues · caches',
    why: 'Kept unlocked precisely because the server trusts run-to-completion atomicity — the very atomicity rerouting removes. Unlocked read-modify-writes lose updates essentially every time they collide.',
    result: 'loses updates on collision — the one pattern the detector guards',
  },
]

// Per-server integrations (paper Table 1: server version, integration point, lines, speedup).
export const INTEGRATIONS = [
  { app: 'Redis',      version: '6.0',  lines: 83,  mod: 0, speedup: '3.01×',  how: 'loadable module (BlockClient)' },
  { app: 'Node.js',    version: '22',   lines: 34,  mod: 0, speedup: '2.54×',  how: 'N-API addon (libuv queue)' },
  { app: 'Python',     version: '3.10', lines: 22,  mod: 0, speedup: '2.37×',  how: 'run_in_executor — one line at the call site' },
  { app: 'nginx',      version: '1.18', lines: 112, mod: 0, speedup: '2.74×',  how: 'addon module (thread pool + aio)' },
  { app: 'memcached',  version: '1.6',  lines: 70,  mod: 1, speedup: '2.93×',  how: 'state-machine patch — the one server with no deferred-response primitive' },
  { app: 'Apache',     version: '2.4',  lines: 27,  mod: 0, speedup: '3.45×',  how: 'module (apxs), pooled workers overlap for free' },
  { app: 'Go',         version: '1.18', lines: 28,  mod: 0, speedup: '3.01×',  how: 'plain blocking cgo call; the goroutine scheduler is the routing' },
  { app: 'PostgreSQL', version: '14',   lines: 42,  mod: 0, speedup: '2.59× †', how: 'C extension (intra-query pipelining)' },
  { app: 'MariaDB',    version: '10.6', lines: 34,  mod: 0, speedup: '2.59× †', how: 'UDF (intra-query pipelining)' },
  { app: 'HAProxy',    version: '2.4',  lines: 138, mod: 0, speedup: '2.10×',  how: 'SPOE + standalone C agent (the proxy changes only configuration)' },
]

export const LINKS = {
  arxiv: 'https://arxiv.org/abs/2607.02630',
  pdf: 'https://github.com/19PINE-AI/transparent-offload/blob/main/paper/paper.pdf',
  code: 'https://github.com/19PINE-AI/transparent-offload',
  site: 'https://01.me/research/transparent-offload',
}

export const BIBTEX = `@misc{transparentoffload,
  author        = {Li, Bojie},
  title         = {Fine-Grained Computation Offload for Off-the-Shelf Servers in Tens of Lines},
  year          = {2026},
  eprint        = {2607.02630},
  archivePrefix = {arXiv},
  url           = {https://arxiv.org/abs/2607.02630},
}`

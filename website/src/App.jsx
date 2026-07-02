import { COLORS } from './data'
import Nav from './components/Nav'
import Hero from './components/Hero'
import Section from './components/Section'
import Reveal from './components/Reveal'
import PipelineAnim from './components/PipelineAnim'
import LandscapeViz from './components/LandscapeViz'
import FiberRuntimeAnim from './components/FiberRuntimeAnim'
import WallsViz from './components/WallsViz'
import RecipeViz from './components/RecipeViz'
import CodeExample from './components/CodeExample'
import RegimeCards from './components/RegimeCards'
import IntegrationsTable from './components/IntegrationsTable'
import PatternCards from './components/PatternCards'
import RaceAnim from './components/RaceAnim'
import Footer from './components/Footer'
import HeadlineChart from './charts/HeadlineChart'
import SpectrumChart from './charts/SpectrumChart'
import WeightChart from './charts/WeightChart'
import LatencyChart from './charts/LatencyChart'
import ClassifierChart from './charts/ClassifierChart'
import { LostUpdatesChart, DetectorThroughputChart } from './charts/CorrectnessCharts'

export default function App() {
  return (
    <>
      <Nav />
      <Hero />

      <Section
        id="problem"
        kicker="01 · Motivation"
        title="The fine-grained offload stall: blocking and busy-waiting both fail"
        lede={<>
          GPUs, TPUs, FPGAs — and increasingly <em>remote</em> calls to HSMs, post-quantum KEM services,
          and inference servers — all give a serving request the same shape: receive, pre-process,
          <strong> offload</strong>, post-process, respond. For fine-grained offloads (microseconds to a
          few milliseconds) the textbook answers fail: <strong>blocking</strong> pays a context switch
          comparable to the offload itself (the “killer microsecond”), and <strong>busy-waiting</strong>{' '}
          burns the core. The only principled answer is to <strong>overlap</strong> the offload with other
          requests’ work.
        </>}
      >
        <Reveal><PipelineAnim /></Reveal>
        <div style={{ height: 24 }} />
        <Reveal><LandscapeViz /></Reveal>
        <Reveal>
          <p className="lede" style={{ marginTop: 28 }}>
            Overlap requires concurrency at the offload point, and prior systems obtain it by
            <strong> adding</strong> concurrency: an async-framework rewrite, a new runtime or dataplane
            OS, or a hand-tuned point integration. The paper’s starting observation is that <strong>the
            server already has it</strong>. Serving concurrent connections <em>is</em> suspending and
            resuming requests — an event loop parks a connection whenever it waits for a socket, a worker
            pool parks whole requests in its queue, a goroutine scheduler parks tasks at every blocking
            call, a database parks each connection in its own backend. Hiding a fine-grained offload is
            therefore <strong>a routing problem, not a rewrite problem</strong>.
          </p>
        </Reveal>
      </Section>

      <Section
        id="model"
        kicker="02 · Predictive Model"
        title="Concurrency model and offload weight predict speedup and code cost"
        lede={<>
          <strong>Prediction 1:</strong> the server’s concurrency model determines what a synchronous
          offload costs, and therefore what rerouting recovers and how many lines it takes — the first
          question about any server is where its suspend/resume machinery lives.{' '}
          <strong>Prediction 2:</strong> the offload’s weight relative to per-request CPU work determines
          whether overlap pays at all, up to two ceilings: the accelerator’s throughput and the server’s
          own overlap capacity. Real speedups live between these bounds.
        </>}
      >
        <RegimeCards />
        <div style={{ height: 24 }} />
        <Reveal><WeightChart /></Reveal>
      </Section>

      <Section
        id="recipe"
        kicker="03 · Method"
        title="Offload rerouting through existing concurrency, on ten servers"
        accent={COLORS.green}
        lede={<>
          The recipe is the thesis made concrete, and it is uniform across servers: at the offload call
          site, <strong>submit</strong> the work to a background executor; <strong>suspend</strong> the
          request using the server’s own deferred-response primitive; <strong>resume</strong> it to reply
          on completion. Across ten off-the-shelf servers spanning every production concurrency model,
          the change is <strong>22–138 lines added, at most one existing line modified</strong> — small for
          a structural reason, not a lucky one: the integration only bridges the offload to machinery that
          already exists.
        </>}
      >
        <RecipeViz />
        <div style={{ height: 24 }} />
        <CodeExample />
        <div style={{ height: 24 }} />
        <IntegrationsTable />
      </Section>

      <Section
        id="transparent"
        kicker="04 · Zero-Edit Limit"
        title="Transparent rerouting of unmodified binaries via library interposition"
        lede={<>
          When there is no source access at all — a stock binary, a vendor blob — the recipe cannot be
          applied from inside. But there is one suspension layer every dynamically linked binary passes
          through: <strong>the libc symbol table</strong>. An <code>LD_PRELOAD</code> fiber runtime
          interposes the standard threading and I/O symbols and injects the recipe’s submit, suspend, and
          resume <strong>from outside the binary</strong> — the handler keeps its plain synchronous shape
          and never learns its “thread” is a fiber.
        </>}
      >
        <Reveal><FiberRuntimeAnim /></Reveal>
        <Reveal>
          <p className="note" style={{ marginTop: 18 }}>
            Making stock binaries run took a catalog of interposition engineering — most notably a glibc
            condition-variable <em>symbol-versioning</em> hazard: the functions carry two incompatible ABIs
            under one name (<code>GLIBC_2.2.5</code> and <code>GLIBC_2.3.2</code>), so a naive interposer
            that exports an unversioned <code>pthread_cond_signal</code> silently corrupts some binaries
            (MariaDB crashes at startup with a wild pointer) while sparing Redis, memcached, nginx, and
            stunnel. The fix — version-matched interposition via a linker version script and{' '}
            <code>dlvsym</code> — is mandatory for any threading-interposing tool. A DNN-inference server
            built from separate source shows 11.8× under the same runtime.
          </p>
        </Reveal>
      </Section>

      <Section
        id="envelope"
        kicker="05 · Interposition Envelope"
        title="The limits of transparency: three escapes from the interposed layer"
        accent={COLORS.red}
        lede={<>
          A preloaded library virtualizes exactly one layer, the libc symbol boundary, so its reach is the
          completeness of that layer — and there are exactly <strong>three ways behavior escapes it</strong>:
          below it, beside it, and behind it. These are properties of where an application places its
          behavior, not gaps in engineering; no interposition removes them. Crucially, an event-driven
          server has no per-connection thread to fiberize, so the class where a synchronous offload is most
          catastrophic is the class the zero-edit limit cannot help — for everyone else, the few-line
          recipe is the answer.
        </>}
      >
        <WallsViz />
        <div style={{ height: 24 }} />
        <Reveal><ClassifierChart /></Reveal>
      </Section>

      <Section
        id="correctness"
        kicker="06 · Correctness"
        title="Rerouting suspends run-to-completion atomicity: a measured taxonomy and a guard"
        accent={COLORS.red}
        lede={<>
          A single-threaded server runs each handler to completion, so handlers observe shared state
          atomically — an invariant the code <em>silently</em> relies on precisely because the architecture
          provides it for free. Rerouting suspends the handler mid-request and breaks that invariant. But a
          measured taxonomy shows the hazard is <strong>one pattern, not a general danger</strong>: of the
          four shared-state patterns real offload-adjacent servers keep, three are unconditionally safe.
        </>}
      >
        <PatternCards />
        <div style={{ height: 28 }} />
        <Reveal><RaceAnim /></Reveal>
        <div style={{ height: 24 }} />
        <div className="grid-2">
          <Reveal><LostUpdatesChart /></Reveal>
          <Reveal delay={120}><DetectorThroughputChart /></Reveal>
        </div>
      </Section>

      <Section
        id="results"
        kicker="07 · Evaluation"
        title="Validation of both predictions on real hardware"
        lede={<>
          All cross-server offloads are real — GPU AES on its own CUDA stream on an NVIDIA RTX PRO 6000,
          and a real TCP signer doing genuine RSA-2048 signatures for the latency-bound remote class
          (HSM signatures, post-quantum KEMs, remote inference). No emulated latencies, no failed requests.
        </>}
      >
        <Reveal><HeadlineChart /></Reveal>
        <div style={{ height: 24 }} />
        <Reveal><SpectrumChart /></Reveal>
        <div style={{ height: 24 }} />
        <Reveal><LatencyChart /></Reveal>
        <Reveal>
          <p className="note" style={{ marginTop: 18 }}>
            The two ceilings, concretely: the 1 MiB GPU AES offload is bandwidth-bound, so the reroutes
            cluster at 2–3.5× — overlap fills the device but cannot exceed it (the first ceiling). A
            latency-bound offload lifts that ceiling: with the real RSA-2048 remote signer (821 µs
            round-trip) the same Python server reaches 3.53× — but not the order of magnitude a deep queue
            could in principle give, because the bottleneck moves to the second ceiling, the server’s own
            overlap capacity (the <code>asyncio</code>/GIL executor keeps only ~6 requests truly in
            flight). HAProxy isolates where the win comes from: its gain rides entirely on the offload
            agent — a 21-line GIL-bound Python agent gains nothing, the 138-line C pthread-pool agent
            recovers 2.1× on the same offload.
          </p>
        </Reveal>
      </Section>

      <Section
        id="conclusion"
        kicker="08 · Conclusion"
        title="Fine-grained offload latency can be hidden in tens of lines"
        lede={<>
          What should the CPU do while it waits for the accelerator? Overlap the offload with other
          requests — and it does not need a new framework, runtime, or operating system to do so, because
          every server that serves concurrent requests already contains the machinery overlap requires.
          Rerouting the offload through that machinery takes <strong>tens of lines</strong> across every
          concurrency model (<strong>1.2–5.4×</strong> on real hardware), is <strong>predictable in
          advance</strong> from the concurrency model and the offload’s weight, can occasionally be done
          with <strong>zero lines</strong> from outside the binary (<strong>17.3×</strong>, within a
          precisely characterized envelope), and <strong>stays correct</strong> with one targeted guard for
          the run-to-completion atomicity it suspends. The model is also a stop sign: when per-request CPU
          rivals the offload, or the accelerator is already saturated, rerouting buys complexity for
          nothing — buy bandwidth instead.
        </>}
      />

      <Footer />
    </>
  )
}

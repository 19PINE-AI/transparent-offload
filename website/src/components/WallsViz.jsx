import { COLORS, WALLS } from '../data'
import Reveal from './Reveal'

function LayerDiagram() {
  return (
    <div className="anim-frame" style={{ marginBottom: 24 }}>
      <svg viewBox="0 0 880 300" role="img"
        aria-label="Three-layer diagram: application, libc symbol layer, kernel and hardware. The interposition layer is libc symbols; a raw futex syscall bypasses it, native TLS thread identity sits above it, and a CPU-bound workload leaves no idle time to reclaim.">
        {/* layers */}
        <rect x="30" y="34" width="820" height="62" rx="10" fill="#EAEDF3" stroke="#c9cfdb" />
        <text x="52" y="70" fontSize="13.5" fontWeight="700" fill={COLORS.ink}>Application / managed runtime</text>

        <rect x="30" y="116" width="820" height="62" rx="10" fill="#DCE6F2" stroke={COLORS.slate} strokeWidth="2" />
        <text x="52" y="145" fontSize="13.5" fontWeight="700" fill={COLORS.slate}>libc symbols — the layer LD_PRELOAD can interpose</text>
        <text x="52" y="163" fontSize="11" fill="#5b6577">pthread_* · read / write / poll · condition variables (version-matched!)</text>

        <rect x="30" y="198" width="820" height="62" rx="10" fill="#EAEDF3" stroke="#c9cfdb" />
        <text x="52" y="234" fontSize="13.5" fontWeight="700" fill={COLORS.ink}>Kernel / hardware</text>

        {/* escape 1: BELOW — raw futex around libc */}
        <path d="M 250 96 C 236 130, 236 170, 250 198" fill="none" stroke={COLORS.red} strokeWidth="3" strokeDasharray="7 4" className="flow-line" />
        <polygon points="250,198 242,186 256,188" fill={COLORS.red} />
        <text x="268" y="222" fontSize="11.5" fontWeight="700" fill={COLORS.red}>BELOW the layer</text>
        <text x="268" y="237" fontSize="10.5" fill={COLORS.red}>raw syscall(futex) bypasses pthread (InnoDB)</text>

        {/* escape 2: BESIDE — native TLS register in the app layer */}
        <rect x="530" y="42" width="118" height="46" rx="8" fill="#fff" stroke={COLORS.red} strokeWidth="2" />
        <text x="589" y="61" textAnchor="middle" fontSize="11.5" fontWeight="700" fill={COLORS.red}>%fs → thread</text>
        <text x="589" y="77" textAnchor="middle" fontSize="10.5" fill={COLORS.red}>native TLS register</text>
        <text x="662" y="61" fontSize="11.5" fontWeight="700" fill={COLORS.red}>BESIDE the layer</text>
        <text x="662" y="77" fontSize="10.5" fill={COLORS.red}>JVM · Go · .NET</text>

        {/* escape 3: BEHIND — no idle CPU */}
        <rect x="742" y="42" width="96" height="46" rx="8" fill="#fff" stroke={COLORS.red} strokeWidth="2" />
        <text x="790" y="61" textAnchor="middle" fontSize="11.5" fontWeight="700" fill={COLORS.red}>CPU 100%</text>
        <text x="790" y="77" textAnchor="middle" fontSize="10.5" fill={COLORS.red}>BEHIND: no idle</text>

        <text x="440" y="290" textAnchor="middle" fontSize="12" fill="#5b6577" fontStyle="italic">
          a preloaded library virtualizes exactly one layer — its reach is the completeness of that layer
        </text>
      </svg>
    </div>
  )
}

export default function WallsViz() {
  return (
    <>
      <Reveal><LayerDiagram /></Reveal>
      <div className="grid-3">
        {WALLS.map((w, i) => (
          <Reveal key={w.n} delay={i * 100}>
            <div className="panel wall-card">
              <div className="wall-num">{w.escape}</div>
              <h4>{w.title}</h4>
              <p>{w.what}</p>
              <div className="wall-evidence">{w.evidence}</div>
            </div>
          </Reveal>
        ))}
      </div>
    </>
  )
}

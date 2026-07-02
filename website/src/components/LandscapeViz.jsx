import { COLORS } from '../data'

// log10(ns) → x. Axis spans 0.2 µs .. 30 ms.
const L = Math.log10(2e2), R = Math.log10(3e7)
const x = (ns) => 60 + ((Math.log10(ns) - L) / (R - L)) * 760

const ITEMS = [
  { ns: 2.5e3, label: 'OS context switch', sub: '~1–5 µs', color: COLORS.red, lift: 66 },
  { ns: 3e4, label: 'GPU AES / small kernel', sub: '~10–50 µs', color: COLORS.slate, lift: 118 },
  { ns: 2.5e5, label: 'compression / small inference', sub: '~0.1–0.5 ms', color: COLORS.teal, lift: 66 },
  { ns: 3e6, label: 'HSM sign · PQC KEM · remote inference', sub: '~1–10 ms', color: COLORS.amber, lift: 118 },
]
const TICKS = [
  [1e3, '1 µs'], [1e4, '10 µs'], [1e5, '100 µs'], [1e6, '1 ms'], [1e7, '10 ms'],
]

export default function LandscapeViz() {
  const AXIS_Y = 208
  return (
    <div className="anim-frame">
      <svg viewBox="0 0 880 268" role="img"
        aria-label="Log-scale latency axis from one microsecond to ten milliseconds. GPU kernels, compression, small inference, HSM signatures and post-quantum KEMs all fall in a shaded fine-grained band where the offload latency is comparable to an OS context switch.">
        {/* fine-grained band: ~1µs .. ~1ms */}
        <rect x={x(1e3)} y={30} width={x(1e6) - x(1e3)} height={AXIS_Y - 22} rx={10}
          fill="#E08A1E" fillOpacity="0.14" className="pulsing" />
        <text x={(x(1e3) + x(1e6)) / 2} y={48} textAnchor="middle" fontSize="12.5"
          fontStyle="italic" fontWeight="600" fill="#8a6d1e">
          the fine-grained regime — offload ≈ scheduling cost
        </text>

        {/* axis */}
        <line x1={x(2.5e2)} y1={AXIS_Y} x2={x(2.6e7)} y2={AXIS_Y} stroke={COLORS.ink} strokeWidth="1.4" />
        <polygon points={`${x(2.6e7) + 8},${AXIS_Y} ${x(2.6e7) - 2},${AXIS_Y - 5} ${x(2.6e7) - 2},${AXIS_Y + 5}`} fill={COLORS.ink} />
        {TICKS.map(([ns, t]) => (
          <g key={t}>
            <line x1={x(ns)} y1={AXIS_Y - 4} x2={x(ns)} y2={AXIS_Y + 4} stroke={COLORS.ink} strokeWidth="1.2" />
            <text x={x(ns)} y={AXIS_Y + 22} textAnchor="middle" fontSize="11.5" fill="#5b6577">{t}</text>
          </g>
        ))}
        <text x={x(2.6e7)} y={AXIS_Y + 40} textAnchor="end" fontSize="11" fill="#8a92a5">offload latency (log scale)</text>

        {/* items */}
        {ITEMS.map((it) => (
          <g key={it.label}>
            <line x1={x(it.ns)} y1={AXIS_Y - 8} x2={x(it.ns)} y2={AXIS_Y - it.lift + 18} stroke={it.color} strokeWidth="1.4" />
            <circle cx={x(it.ns)} cy={AXIS_Y - 8} r="7" fill={it.color} />
            <text x={x(it.ns)} y={AXIS_Y - it.lift + 2} textAnchor="middle" fontSize="12" fontWeight="700" fill={COLORS.ink}>
              {it.label}
            </text>
            <text x={x(it.ns)} y={AXIS_Y - it.lift + 16} textAnchor="middle" fontSize="10.5" fill="#5b6577">
              {it.sub}
            </text>
          </g>
        ))}
      </svg>
      <div className="anim-caption">
        In the shaded band the offload finishes on the timescale of a context switch: blocking pays a switch
        and a wakeup that rival the work itself, and busy-waiting wastes a core. This is exactly where
        overlapping the offload with other requests is the right answer.
      </div>
    </div>
  )
}

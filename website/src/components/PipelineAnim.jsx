import { COLORS } from '../data'

// Timeline geometry (one shared time axis, revealed by an animated clip "playhead").
const X0 = 130
const XMAX = 830
const DUR = 8 // seconds per loop

const REQ = [
  { name: 'A', color: COLORS.slate },
  { name: 'B', color: COLORS.teal },
  { name: 'C', color: COLORS.green },
]

// Synchronous: pre(70) -> offload(120, CPU stalls) -> post(70), back to back.
const syncSpans = REQ.map((r, i) => {
  const s = X0 + i * 260
  return { ...r, pre: [s, s + 70], off: [s + 70, s + 190], post: [s + 190, s + 260] }
})

// Overlapped: CPU pipelines pre/post of other requests during offloads.
const ovCpu = [
  { r: 0, kind: 'pre', span: [X0, X0 + 70] },
  { r: 1, kind: 'pre', span: [X0 + 70, X0 + 140] },
  { r: 2, kind: 'pre', span: [X0 + 140, X0 + 210] },
  { r: 0, kind: 'post', span: [X0 + 210, X0 + 280] },
  { r: 1, kind: 'post', span: [X0 + 280, X0 + 350] },
  { r: 2, kind: 'post', span: [X0 + 350, X0 + 420] },
]
const ovAccel = [
  { r: 0, span: [X0 + 70, X0 + 190] },
  { r: 1, span: [X0 + 140, X0 + 260] },
  { r: 2, span: [X0 + 210, X0 + 330] },
]

function Bar({ x1, x2, y, color, label, hollow, h = 26 }) {
  return (
    <g>
      <rect
        x={x1} y={y} width={x2 - x1} height={h} rx={5}
        fill={hollow ? 'none' : color}
        stroke={color} strokeWidth={hollow ? 1.6 : 0}
        strokeDasharray={hollow ? '4 3' : undefined}
        fillOpacity={hollow ? 0 : 0.92}
      />
      {label && (
        <text x={(x1 + x2) / 2} y={y + h / 2 + 4} textAnchor="middle"
          fontSize="11" fontWeight="700" fill={hollow ? color : '#fff'}>
          {label}
        </text>
      )}
    </g>
  )
}

function Lane({ y, label }) {
  return (
    <g>
      <text x={X0 - 14} y={y + 17} textAnchor="end" fontSize="12" fontWeight="600" fill={COLORS.ink}>
        {label}
      </text>
      <line x1={X0} y1={y + 13} x2={XMAX + 10} y2={y + 13} stroke="#dcd9d0" strokeWidth="1" />
    </g>
  )
}

function PanelTitle({ y, text, sub, color }) {
  return (
    <g>
      <text x={24} y={y} fontSize="14" fontWeight="800" fill={color}>{text}</text>
      <text x={24} y={y + 17} fontSize="11.5" fill="#5b6577">{sub}</text>
    </g>
  )
}

export default function PipelineAnim() {
  const clipId = 'playclip'
  return (
    <div className="anim-frame">
      <svg viewBox="0 0 880 430" role="img"
        aria-label="Animated timeline comparing a synchronous offload, where the CPU stalls, with overlapped offloads, where the CPU processes other requests during each offload.">
        <defs>
          <clipPath id={clipId}>
            <rect x={X0} y="0" height="430" width="0">
              <animate attributeName="width" from="0" to={XMAX - X0 + 30} dur={`${DUR}s`} repeatCount="indefinite" />
            </rect>
          </clipPath>
          <pattern id="stall" width="8" height="8" patternTransform="rotate(45)" patternUnits="userSpaceOnUse">
            <rect width="8" height="8" fill="rgba(176,36,27,0.08)" />
            <line x1="0" y1="0" x2="0" y2="8" stroke="rgba(176,36,27,0.5)" strokeWidth="2.5" />
          </pattern>
        </defs>

        {/* ===== top panel: synchronous ===== */}
        <PanelTitle y={34} color={COLORS.red}
          text="Synchronous offload — the CPU stalls"
          sub="one request at a time: each offload wastes the core (or a context switch that costs as much as the gap)" />
        <Lane y={62} label="CPU" />
        <Lane y={106} label="accelerator" />

        <g clipPath={`url(#${clipId})`}>
          {syncSpans.map((s) => (
            <g key={s.name}>
              <Bar x1={s.pre[0]} x2={s.pre[1]} y={62} color={s.color} label={`pre ${s.name}`} />
              {/* stall on the CPU lane while the device works */}
              <rect x={s.off[0]} y={62} width={s.off[1] - s.off[0]} height={26} rx={5}
                fill="url(#stall)" stroke={COLORS.red} strokeWidth="1.2" strokeDasharray="3 3" />
              <text x={(s.off[0] + s.off[1]) / 2} y={79} textAnchor="middle" fontSize="10.5"
                fontWeight="700" fill={COLORS.red}>stall</text>
              <Bar x1={s.off[0]} x2={s.off[1]} y={106} color={COLORS.amber} label={`offload ${s.name}`} />
              <Bar x1={s.post[0]} x2={s.post[1]} y={62} color={s.color} label={`post ${s.name}`} />
            </g>
          ))}
          <g>
            <text x={syncSpans[2].post[1] + 10} y={80} fontSize="12" fontWeight="700" fill={COLORS.red}>
              3 done
            </text>
          </g>
        </g>

        {/* divider */}
        <line x1="24" y1="168" x2="856" y2="168" stroke="#e3e0d8" strokeWidth="1" />

        {/* ===== bottom panel: overlapped ===== */}
        <PanelTitle y={200} color={COLORS.slate}
          text="Overlapped offload — the CPU fills the gap with other requests"
          sub="the same three requests finish in barely more than half the time; the accelerator stays busy too" />
        <Lane y={228} label="CPU" />
        <Lane y={272} label="accelerator" />

        <g clipPath={`url(#${clipId})`}>
          {ovCpu.map((seg, i) => (
            <Bar key={i} x1={seg.span[0]} x2={seg.span[1]} y={228}
              color={REQ[seg.r].color} label={`${seg.kind} ${REQ[seg.r].name}`} />
          ))}
          {ovAccel.map((seg, i) => (
            <Bar key={i} x1={seg.span[0]} x2={seg.span[1]} y={264 + i * 12}
              color={COLORS.amber} label={`offload ${REQ[seg.r].name}`} h={20} />
          ))}
          <g>
            <text x={ovCpu[5].span[1] + 10} y={246} fontSize="12" fontWeight="700" fill={COLORS.green}>
              3 done ✓
            </text>
            <rect x={ovCpu[5].span[1] + 4} y={258} width={2} height={54} fill={COLORS.green} opacity="0.6" />
            <text x={ovCpu[5].span[1] + 12} y={296} fontSize="11" fill={COLORS.green} fontWeight="600">
              time saved →
            </text>
          </g>
        </g>

        {/* playhead */}
        <g>
          <line x1={X0} y1="46" x2={X0} y2="316" stroke={COLORS.ink} strokeWidth="1.5" opacity="0.55">
            <animate attributeName="x1" from={X0} to={XMAX + 30} dur={`${DUR}s`} repeatCount="indefinite" />
            <animate attributeName="x2" from={X0} to={XMAX + 30} dur={`${DUR}s`} repeatCount="indefinite" />
          </line>
        </g>

        {/* time axis */}
        <line x1={X0} y1="336" x2={XMAX + 20} y2="336" stroke={COLORS.ink} strokeWidth="1.2" />
        <polygon points={`${XMAX + 20},336 ${XMAX + 10},331 ${XMAX + 10},341`} fill={COLORS.ink} />
        <text x={(X0 + XMAX) / 2} y="356" textAnchor="middle" fontSize="11.5" fill="#5b6577">time →</text>

        {/* legend */}
        <g transform="translate(130, 380)" fontSize="11.5" fill={COLORS.ink}>
          <rect x="0" y="0" width="14" height="14" rx="3" fill={COLORS.slate} />
          <text x="20" y="11">CPU work (pre / post)</text>
          <rect x="185" y="0" width="14" height="14" rx="3" fill={COLORS.amber} />
          <text x="205" y="11">offload in flight on the device</text>
          <rect x="425" y="0" width="14" height="14" rx="3" fill="url(#stall)" stroke={COLORS.red} strokeWidth="1" />
          <text x="445" y="11">CPU stalled waiting</text>
        </g>
      </svg>
      <div className="anim-caption">
        A serving request is <b>receive → pre-process → offload → post-process → send</b>. For fine-grained
        offloads (microseconds to a few milliseconds), a context switch costs as much as the gap itself and
        busy-waiting burns the core — overlap is the only answer that wastes neither CPU nor latency.
      </div>
    </div>
  )
}

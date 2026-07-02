import { COLORS } from '../data'

const DUR = '7s'

// One scheduling cycle: A runs → parks at its offload → B runs → parks → C runs →
// A's completion arrives → A resumes → B resumes. All as SMIL keyTime tracks.
const RUN = {
  A: { values: '1;0;0;1;0', keyTimes: '0;0.15;0.6;0.65;0.85' },
  B: { values: '0;1;0;0;1', keyTimes: '0;0.15;0.4;0.8;0.85' },
  C: { values: '0;0;1;0;0', keyTimes: '0;0.4;0.42;0.65;1' },
}
const PARK = {
  A: { values: '0;1;0;0', keyTimes: '0;0.15;0.65;1' },
  B: { values: '0;1;0;0', keyTimes: '0;0.4;0.85;1' },
  C: { values: '0;0;1;1', keyTimes: '0;0.65;0.67;1' },
}
const OPS = {
  A: { values: '0;1;0;0', keyTimes: '0;0.15;0.62;1' },
  B: { values: '0;1;0;0', keyTimes: '0;0.4;0.83;1' },
  C: { values: '0;0;1;1', keyTimes: '0;0.65;0.67;1' },
}

const FIBERS = [
  { id: 'A', label: 'fiber A · connection 1', color: COLORS.teal, y: 96 },
  { id: 'B', label: 'fiber B · connection 2', color: COLORS.green, y: 156 },
  { id: 'C', label: 'fiber C · connection 3', color: COLORS.purple, y: 216 },
]

function track(anim, attr = 'opacity', discrete = true) {
  return (
    <animate
      attributeName={attr}
      values={anim.values}
      keyTimes={anim.keyTimes}
      calcMode={discrete ? 'discrete' : 'linear'}
      dur={DUR}
      repeatCount="indefinite"
    />
  )
}

export default function FiberRuntimeAnim() {
  return (
    <div className="anim-frame">
      <svg viewBox="0 0 880 400" role="img"
        aria-label="Animated diagram of the LD_PRELOAD fiber runtime: three fibers take turns running on one carrier core; when a fiber submits an offload it parks and the scheduler runs another fiber until the accelerator completes.">
        <defs>
          <marker id="arrAmber" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M0,0 L10,5 L0,10 z" fill={COLORS.amber} />
          </marker>
          <marker id="arrSlate" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M0,0 L10,5 L0,10 z" fill={COLORS.slate} />
          </marker>
        </defs>

        {/* LD_PRELOAD wrapper */}
        <rect x="20" y="18" width="510" height="348" rx="16" fill="none" stroke={COLORS.slate} strokeWidth="1.6" strokeDasharray="7 5" />
        <text x="36" y="44" fontSize="13" fontWeight="800" fill={COLORS.slate} fontFamily="JetBrains Mono, monospace">
          LD_PRELOAD runtime
        </text>
        <text x="36" y="62" fontSize="11" fill="#5b6577">application binary unchanged — pthread_create → fiber, read/write/poll → yield</text>

        {/* carrier core */}
        <rect x="36" y="76" width="478" height="200" rx="12" fill="#EAEDF3" stroke={COLORS.slate} strokeWidth="1.4" />
        <text x="48" y="94" fontSize="11.5" fontWeight="700" fill={COLORS.slate}>carrier core — one OS thread</text>

        {FIBERS.map(f => (
          <g key={f.id}>
            {/* running glow */}
            <rect x="48" y={f.y} width="380" height="46" rx="9" fill={f.color} opacity="0">
              {track(RUN[f.id])}
            </rect>
            <rect x="48" y={f.y} width="380" height="46" rx="9" fill="#fff" stroke={f.color} strokeWidth="1.8" fillOpacity="0.35" />
            <text x="64" y={f.y + 28} fontSize="12.5" fontWeight="700" fill={COLORS.ink}>{f.label}</text>
            {/* running badge */}
            <g opacity="0">
              {track(RUN[f.id])}
              <rect x="318" y={f.y + 11} width="98" height="24" rx="12" fill="#fff" opacity="0.92" />
              <circle cx="333" cy={f.y + 23} r="4.5" fill={f.color} />
              <text x="343" y={f.y + 27} fontSize="11" fontWeight="700" fill={f.color}>running</text>
            </g>
            {/* parked badge */}
            <g opacity="0">
              {track(PARK[f.id])}
              <rect x="300" y={f.y + 11} width="116" height="24" rx="12" fill="#fff" stroke="#d8d5cc" />
              <text x="311" y={f.y + 27} fontSize="11" fontWeight="600" fill="#8a92a5">⏸ offload in flight</text>
            </g>
          </g>
        ))}

        {/* scheduler */}
        <rect x="36" y="292" width="478" height="56" rx="12" fill={COLORS.slate} />
        <text x="275" y="315" textAnchor="middle" fontSize="12.5" fontWeight="700" fill="#fff">
          scheduler — register-only fiber switch, tens of nanoseconds
        </text>
        <text x="275" y="334" textAnchor="middle" fontSize="11" fill="#c8d2e4">
          epoll for socket readiness · polls the device for offload completions
        </text>

        {/* accelerator */}
        <rect x="640" y="96" width="216" height="200" rx="14" fill={COLORS.amber} fillOpacity="0.14" stroke={COLORS.amber} strokeWidth="1.8" />
        <text x="748" y="126" textAnchor="middle" fontSize="13.5" fontWeight="800" fill="#a4650e">accelerator</text>
        <text x="748" y="143" textAnchor="middle" fontSize="10.5" fill="#a4650e">GPU · HSM · remote service</text>
        {FIBERS.map((f, i) => (
          <g key={f.id} opacity="0">
            {track(OPS[f.id])}
            <rect x="662" y={162 + i * 38} width="172" height="26" rx="7" fill={COLORS.amber} opacity="0.9" />
            <text x="748" y={179 + i * 38} textAnchor="middle" fontSize="11" fontWeight="700" fill="#fff">
              offload {f.id} running…
            </text>
          </g>
        ))}

        {/* submit + completion arrows */}
        <g>
          <path d="M 530 150 C 585 140, 600 140, 638 150" fill="none" stroke={COLORS.amber} strokeWidth="2.4" markerEnd="url(#arrAmber)" className="flow-line" />
          <text x="585" y="128" textAnchor="middle" fontSize="11.5" fontWeight="700" fill="#a4650e">submit + yield</text>
        </g>
        <g>
          <path d="M 638 250 C 600 262, 585 290, 516 306" fill="none" stroke={COLORS.slate} strokeWidth="2.4" markerEnd="url(#arrSlate)" className="flow-line" />
          <text x="592" y="332" textAnchor="middle" fontSize="11.5" fontWeight="700" fill={COLORS.slate}>completion → resume</text>
        </g>
      </svg>
      <div className="anim-caption">
        Loaded ahead of the application by <code>LD_PRELOAD</code>, the runtime turns each
        connection-handling thread into a <b>fiber</b> on one carrier core. A fiber that would block —
        on socket I/O or on the offload — yields in tens of nanoseconds and another fiber runs; the handler
        keeps its plain synchronous shape and never learns its “thread” is a fiber. On a real GPU this
        overlaps 64 connections’ offloads: <b>17.3×</b> over busy-wait, <b>11.9×</b> over OS-thread blocking.
      </div>
    </div>
  )
}

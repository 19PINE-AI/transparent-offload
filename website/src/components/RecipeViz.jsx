import { COLORS } from '../data'
import Reveal from './Reveal'

const STEPS = [
  {
    n: 1, color: COLORS.green, title: 'Submit',
    text: 'At the offload call site, hand the work to a background executor instead of waiting on it.',
  },
  {
    n: 2, color: COLORS.purple, title: 'Suspend',
    text: 'Park the request using the server’s own deferred-response primitive — the machinery it already uses to serve many connections at once.',
  },
  {
    n: 3, color: COLORS.slate, title: 'Resume',
    text: 'When the accelerator completes, resume the request and send the reply. Meanwhile the loop has been serving other requests the whole time.',
  },
]

function RecipeFlow() {
  return (
    <div className="anim-frame" style={{ marginBottom: 24 }}>
      <svg viewBox="0 0 880 250" role="img"
        aria-label="Flow diagram of the minimal-edit recipe: the handler submits the offload to a background executor, suspends the request with the server's own primitive while the event loop serves other requests, and resumes it to reply when the accelerator completes.">
        <defs>
          <marker id="rArr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M0,0 L10,5 L0,10 z" fill={COLORS.gray} />
          </marker>
        </defs>

        {/* handler */}
        <rect x="30" y="46" width="170" height="62" rx="11" fill={COLORS.slate} />
        <text x="115" y="72" textAnchor="middle" fontSize="12.5" fontWeight="700" fill="#fff">handler reaches</text>
        <text x="115" y="90" textAnchor="middle" fontSize="12.5" fontWeight="700" fill="#fff">the offload</text>

        {/* 1 submit */}
        <rect x="280" y="40" width="200" height="62" rx="11" fill="#fff" stroke={COLORS.green} strokeWidth="2" />
        <circle cx="304" cy="60" r="11" fill={COLORS.green} />
        <text x="304" y="64.5" textAnchor="middle" fontSize="12" fontWeight="800" fill="#fff">1</text>
        <text x="324" y="65" fontSize="12.5" fontWeight="700" fill={COLORS.ink}>submit to a</text>
        <text x="324" y="83" fontSize="12.5" fontWeight="700" fill={COLORS.ink}>background executor</text>
        <path d="M 200 71 L 276 71" stroke={COLORS.green} strokeWidth="2.2" markerEnd="url(#rArr)" className="flow-line" />

        {/* accelerator */}
        <rect x="600" y="40" width="250" height="62" rx="11" fill={COLORS.amber} />
        <text x="725" y="66" textAnchor="middle" fontSize="12.5" fontWeight="700" fill="#fff">accelerator runs the offload</text>
        <text x="725" y="86" textAnchor="middle" fontSize="10.5" fill="#fff" opacity="0.9">GPU AES · RSA signer · inference</text>
        <path d="M 480 71 L 596 71" stroke={COLORS.amber} strokeWidth="2.2" markerEnd="url(#rArr)" className="flow-line" />

        {/* 2 suspend */}
        <rect x="280" y="152" width="200" height="62" rx="11" fill="#fff" stroke={COLORS.purple} strokeWidth="2" />
        <circle cx="304" cy="172" r="11" fill={COLORS.purple} />
        <text x="304" y="176.5" textAnchor="middle" fontSize="12" fontWeight="800" fill="#fff">2</text>
        <text x="324" y="177" fontSize="12.5" fontWeight="700" fill={COLORS.ink}>suspend the request</text>
        <text x="324" y="195" fontSize="11" fill="#5b6577">server’s own primitive</text>
        <path d="M 130 108 C 150 150, 200 172, 276 180" fill="none" stroke={COLORS.purple} strokeWidth="2.2" markerEnd="url(#rArr)" className="flow-line" />

        {/* loop keeps serving */}
        <rect x="30" y="152" width="170" height="62" rx="11" fill="#EAEDF3" stroke={COLORS.teal} strokeWidth="1.8" />
        <text x="115" y="178" textAnchor="middle" fontSize="12.5" fontWeight="700" fill={COLORS.teal}>loop serves</text>
        <text x="115" y="196" textAnchor="middle" fontSize="12.5" fontWeight="700" fill={COLORS.teal}>other requests ↻</text>

        {/* 3 resume */}
        <rect x="600" y="152" width="250" height="62" rx="11" fill="#fff" stroke={COLORS.slate} strokeWidth="2" />
        <circle cx="624" cy="172" r="11" fill={COLORS.slate} />
        <text x="624" y="176.5" textAnchor="middle" fontSize="12" fontWeight="800" fill="#fff">3</text>
        <text x="644" y="177" fontSize="12.5" fontWeight="700" fill={COLORS.ink}>resume + reply</text>
        <text x="644" y="195" fontSize="11" fill="#5b6577">on completion</text>
        <path d="M 725 102 L 725 148" stroke={COLORS.slate} strokeWidth="2.2" markerEnd="url(#rArr)" className="flow-line" />
        <path d="M 596 183 L 484 183" stroke={COLORS.slate} strokeWidth="2.2" markerEnd="url(#rArr)" className="flow-line" />

        <text x="440" y="242" textAnchor="middle" fontSize="12" fill="#5b6577" fontStyle="italic">
          22–138 lines added, 0–1 modified — the machinery already exists; one only reroutes the offload through it
        </text>
      </svg>
    </div>
  )
}

export default function RecipeViz() {
  return (
    <>
      <Reveal><RecipeFlow /></Reveal>
      <div className="steps">
        {STEPS.map((s, i) => (
          <Reveal key={s.n} delay={i * 100}>
            <div className="step">
              <div className="step-badge" style={{ background: s.color }}>{s.n}</div>
              <h4>{s.title}</h4>
              <p>{s.text}</p>
            </div>
          </Reveal>
        ))}
      </div>
    </>
  )
}

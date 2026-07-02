import { COLORS } from '../data'

const DUR = '9s'

// Staged story, as keyTime tracks over one 9s loop:
// A reads x=5 → A parks at offload → B reads x=5 → B parks → A writes 6 →
// B writes 6 (lost update!) → flash the loss → reset.
function show(values, keyTimes) {
  return (
    <animate attributeName="opacity" values={values} keyTimes={keyTimes}
      calcMode="discrete" dur={DUR} repeatCount="indefinite" />
  )
}

function Step({ x, y, w = 200, color, lines, tv, tk }) {
  return (
    <g opacity="0">
      {show(tv, tk)}
      <rect x={x} y={y} width={w} height={44} rx={9} fill="#fff" stroke={color} strokeWidth="1.8" />
      {lines.map((t, i) => (
        <text key={i} x={x + w / 2} y={y + (lines.length === 1 ? 27 : 19 + i * 17)}
          textAnchor="middle" fontSize="12" fontWeight={i === 0 ? 700 : 500}
          fill={i === 0 ? color : '#5b6577'}>{t}</text>
      ))}
    </g>
  )
}

export default function RaceAnim() {
  return (
    <div className="anim-frame">
      <svg viewBox="0 0 880 330" role="img"
        aria-label="Animated race: request A reads counter 5 and parks at its offload; request B also reads 5 and parks; both write back 6, losing one update. The conflict detector catches the write to a page modified during the park and serializes only the conflicting handler.">
        {/* shared state */}
        <rect x="360" y="24" width="160" height="66" rx="12" fill={COLORS.ink} />
        <text x="440" y="50" textAnchor="middle" fontSize="12" fontWeight="600" fill="#aab3c7">shared key</text>
        <g>
          <text x="440" y="76" textAnchor="middle" fontSize="20" fontWeight="800" fill="#fff" opacity="1">
            x = 5
            <animate attributeName="opacity" values="1;1;0;0;1" keyTimes="0;0.5;0.52;0.97;1" calcMode="discrete" dur={DUR} repeatCount="indefinite" />
          </text>
          <text x="440" y="76" textAnchor="middle" fontSize="20" fontWeight="800" fill="#ffd479" opacity="0">
            x = 6
            <animate attributeName="opacity" values="0;0;1;1;0" keyTimes="0;0.52;0.54;0.97;1" calcMode="discrete" dur={DUR} repeatCount="indefinite" />
          </text>
        </g>

        {/* lanes */}
        <text x="30" y="140" fontSize="13" fontWeight="800" fill={COLORS.teal}>request A</text>
        <line x1="30" y1="150" x2="850" y2="150" stroke="#e3e0d8" />
        <text x="30" y="240" fontSize="13" fontWeight="800" fill={COLORS.purple}>request B</text>
        <line x1="30" y1="250" x2="850" y2="250" stroke="#e3e0d8" />

        {/* A steps */}
        <Step x={30} y={160} color={COLORS.teal} lines={['reads x = 5']} tv="0;1;1" tk="0;0.06;1" />
        <Step x={250} y={160} color={COLORS.amber} lines={['parks at GPU offload…']} tv="0;1;0;0" tk="0;0.14;0.5;1" />
        <Step x={470} y={160} color={COLORS.teal} lines={['writes x = 5+1 = 6']} tv="0;1;1" tk="0;0.5;1" />

        {/* B steps */}
        <Step x={140} y={260} color={COLORS.purple} lines={['reads x = 5  (stale soon!)']} tv="0;1;1" tk="0;0.25;1" />
        <Step x={360} y={260} color={COLORS.amber} lines={['parks at GPU offload…']} tv="0;1;0;0" tk="0;0.33;0.62;1" />
        <Step x={580} y={260} color={COLORS.red} lines={['writes x = 5+1 = 6 ✗']} tv="0;1;1" tk="0;0.62;1" />

        {/* verdicts */}
        <g opacity="0">
          {show('0;1;1', '0;0.72;1')}
          <rect x="690" y="160" width="160" height="44" rx="9" fill={COLORS.red} fillOpacity="0.1" stroke={COLORS.red} strokeWidth="1.6" strokeDasharray="4 3" />
          <text x="770" y="179" textAnchor="middle" fontSize="12" fontWeight="800" fill={COLORS.red}>one update lost</text>
          <text x="770" y="196" textAnchor="middle" fontSize="10.5" fill={COLORS.red}>naive: 23,450 of these</text>
        </g>
        <g opacity="0">
          {show('0;1;1', '0;0.82;1')}
          <rect x="690" y="260" width="160" height="44" rx="9" fill={COLORS.green} fillOpacity="0.1" stroke={COLORS.green} strokeWidth="1.6" />
          <text x="770" y="279" textAnchor="middle" fontSize="12" fontWeight="800" fill={COLORS.green}>detector: caught ✓</text>
          <text x="770" y="296" textAnchor="middle" fontSize="10.5" fill={COLORS.green}>page dirtied during park</text>
        </g>

        <text x="440" y="320" textAnchor="middle" fontSize="11.5" fill="#5b6577" fontStyle="italic">
          the more latency overlap hides, the wider the race it opens — the detector closes it without a lock
        </text>
      </svg>
      <div className="anim-caption">
        The hazardous pattern in action: two requests interleave a read-modify-write on an unlocked shared
        aggregate around parked offloads. The detector write-protects the shared-state pages, snapshots a
        version clock when a handler parks, and treats a write fault on a page modified during the park as
        a conflict — serializing only the conflicting handlers, with no application annotation. It applies
        at every point of the spectrum, including the zero-edit limit.
      </div>
    </div>
  )
}

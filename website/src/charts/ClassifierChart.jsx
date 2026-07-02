import {
  ResponsiveContainer, BarChart, Bar, XAxis, YAxis,
  CartesianGrid, Tooltip, Cell, LabelList,
} from 'recharts'
import { CLASSIFIER } from '../data'
import { ChartCard, Tip, axisStyle, gridStroke } from './common'

// Bars encode log10(futex) so the 500× spread stays readable; labels show real values.
const data = CLASSIFIER.map(d => ({ ...d, logv: Math.log10(d.futex) }))

export default function ClassifierChart() {
  return (
    <ChartCard
      title="Predicting the envelope from the outside: futex density"
      sub="Per-request blocking syscalls profiled under load — a cheap strace tells an operator in advance whether zero-edit rerouting can work."
      footer="Thread-per-connection servers that block at the libc layer (stunnel) show near-zero per-request futex traffic: fiberizable. InnoDB is unmistakable — ~13,000 futex calls per request, some 500× higher, alongside kernel-bypass io_uring: the below-the-layer escape. Event-driven servers show an epoll-dominated profile and have no per-connection thread to fiberize: the runtime loads safely but never engages."
    >
      <ResponsiveContainer width="100%" height={320}>
        <BarChart data={data} margin={{ top: 30, right: 12, bottom: 4, left: 6 }}>
          <CartesianGrid stroke={gridStroke} strokeDasharray="3 3" vertical={false} />
          <XAxis dataKey="app" tick={{ ...axisStyle, fontWeight: 600, fill: '#1a1a2e' }} interval={0} />
          <YAxis domain={[0, 5]} ticks={[0, 1, 2, 3, 4, 5]} tick={axisStyle}
            tickFormatter={(v) => (v === 0 ? '1' : `10${'⁰¹²³⁴⁵'[v]}`)}
            label={{ value: 'per-request futex calls (log)', angle: -90, position: 'insideLeft', style: { ...axisStyle, fontSize: 12 } }} />
          <Tooltip cursor={{ fill: 'rgba(52,80,127,0.05)' }} content={
            <Tip render={(p) => {
              const d = p[0].payload
              return (<><b>{d.app}</b><br />{d.futex.toLocaleString()} futex calls / request<br />{d.kind}</>)
            }} />
          } />
          <Bar dataKey="logv" barSize={64} radius={[7, 7, 0, 0]} animationDuration={900}>
            {data.map(d => <Cell key={d.app} fill={d.color} />)}
            <LabelList position="top" content={({ x, y, width, index }) => {
              const d = data[index]
              return (
                <g>
                  <text x={x + width / 2} y={y - 22} textAnchor="middle" fontSize="13" fontWeight="800" fill={d.color}>
                    {d.futex.toLocaleString()}
                  </text>
                  <text x={x + width / 2} y={y - 8} textAnchor="middle" fontSize="10.5" fontWeight="600" fill="#5b6577">
                    {d.kind}
                  </text>
                </g>
              )
            }} />
          </Bar>
        </BarChart>
      </ResponsiveContainer>
    </ChartCard>
  )
}

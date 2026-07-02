import { useState } from 'react'
import {
  ResponsiveContainer, LineChart, Line, XAxis, YAxis,
  CartesianGrid, Tooltip, ReferenceLine, Legend,
} from 'recharts'
import { LATENCY_OVERLAP, LATENCY_BLOCK, COLORS } from '../data'
import { ChartCard, Tip, axisStyle, gridStroke } from './common'

// Merge the two series (different x samples) into one x-sorted array with nulls.
function merge(metric) {
  const rows = []
  LATENCY_OVERLAP.forEach(d => rows.push({ off: d.off, overlap: d[metric] }))
  LATENCY_BLOCK.forEach(d => rows.push({ off: d.off, block: d[metric] }))
  return rows.sort((a, b) => a.off - b.off)
}

const fmt = (us) =>
  us >= 1000 ? `${(us / 1000).toFixed(us >= 100000 ? 0 : 1)} ms` : `${us.toFixed(1)} µs`

export default function LatencyChart() {
  const [metric, setMetric] = useState('p50')
  const data = merge(metric)
  return (
    <ChartCard
      title="Overlap holds low latency to ~4× the offered load"
      sub="Open-loop Poisson arrivals over a controlled 20 µs emulated offload (the one emulated experiment in the paper, so offered load can be swept precisely)."
      footer="Blocking saturates near 103 K req/s and its latency explodes; the overlapping path holds both median and tail low until its own knee near 410 K req/s. Overlap is a latency win, not only a throughput win."
    >
      <div className="toggle-row">
        <button className={metric === 'p50' ? 'active' : ''} onClick={() => setMetric('p50')}>median (p50)</button>
        <button className={metric === 'p99' ? 'active' : ''} onClick={() => setMetric('p99')}>tail (p99)</button>
      </div>
      <ResponsiveContainer width="100%" height={360}>
        <LineChart data={data} margin={{ top: 26, right: 24, bottom: 6, left: 10 }}>
          <CartesianGrid stroke={gridStroke} strokeDasharray="3 3" />
          <XAxis type="number" dataKey="off" domain={[40, 470]} tick={axisStyle}
            ticks={[50, 100, 200, 300, 400, 460]}
            label={{ value: 'offered load (K req/s)', position: 'insideBottom', offset: -4, style: { ...axisStyle, fontSize: 12 } }} />
          <YAxis scale="log" domain={[15, 600000]} tick={axisStyle}
            ticks={[100, 1000, 10000, 100000]}
            tickFormatter={fmt}
            label={{ value: `${metric} latency (log)`, angle: -90, position: 'insideLeft', offset: -2, style: { ...axisStyle, fontSize: 12 } }} />
          <ReferenceLine x={103} stroke={COLORS.red} strokeDasharray="4 4"
            label={{ value: 'blocking knee ~103K', position: 'top', style: { fontSize: 10.5, fill: COLORS.red, fontWeight: 700 } }} />
          <ReferenceLine x={410} stroke={COLORS.slate} strokeDasharray="4 4"
            label={{ value: 'overlap knee ~410K', position: 'top', style: { fontSize: 10.5, fill: COLORS.slate, fontWeight: 700 } }} />
          <Tooltip content={
            <Tip render={(p) => (<>
              <b>{p[0].payload.off} K req/s offered</b><br />
              {p.map(s => (
                <span key={s.dataKey}>
                  {s.dataKey === 'overlap' ? 'overlap (fibers)' : 'block (thread pool)'}: {fmt(s.value)}<br />
                </span>
              ))}
            </>)} />
          } />
          <Legend wrapperStyle={{ fontSize: 12 }} />
          <Line type="monotone" dataKey="overlap" name="overlap (fibers)" connectNulls
            stroke={COLORS.slate} strokeWidth={2.6} dot={{ r: 4, fill: COLORS.slate }} animationDuration={800} />
          <Line type="monotone" dataKey="block" name="block (thread pool)" connectNulls
            stroke={COLORS.red} strokeWidth={2.6} dot={{ r: 4, fill: COLORS.red }} animationDuration={800} />
        </LineChart>
      </ResponsiveContainer>
    </ChartCard>
  )
}

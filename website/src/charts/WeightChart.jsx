import { useState } from 'react'
import {
  ResponsiveContainer, ComposedChart, Line, Bar, XAxis, YAxis,
  CartesianGrid, Tooltip, ReferenceLine, Legend,
} from 'recharts'
import { BLOCKSIZE, COLORS } from '../data'
import { ChartCard, Tip, axisStyle, gridStroke } from './common'

export default function WeightChart() {
  const [mode, setMode] = useState('speedup')
  return (
    <ChartCard
      title="Prediction 2, measured: overlap pays only when the offload outweighs per-request CPU"
      sub="Real GPU AES on a single-event-loop server (Python asyncio, 50 clients), block size swept 4 KiB → 8 MiB."
      footer="At 4 KiB the offload is launch-bound (46 µs, on the order of the server’s own per-request work): 1.24×, nothing to reclaim. At 8 MiB it is bandwidth-bound (2.3 ms): 5.41×, approaching the GPU’s throughput — the first ceiling. The same weight condition is also a stop sign for the operator, and it raises the behind-the-layer escape of the interposition envelope."
    >
      <div className="toggle-row">
        <button className={mode === 'speedup' ? 'active' : ''} onClick={() => setMode('speedup')}>
          speedup + GPU latency
        </button>
        <button className={mode === 'tput' ? 'active' : ''} onClick={() => setMode('tput')}>
          raw throughput (sync vs async)
        </button>
      </div>
      <ResponsiveContainer width="100%" height={360}>
        {mode === 'speedup' ? (
          <ComposedChart data={BLOCKSIZE} margin={{ top: 8, right: 12, bottom: 6, left: 6 }}>
            <CartesianGrid stroke={gridStroke} strokeDasharray="3 3" />
            <XAxis dataKey="label" tick={axisStyle}
              label={{ value: 'AES block size (offload weight)', position: 'insideBottom', offset: -4, style: { ...axisStyle, fontSize: 12 } }} />
            <YAxis yAxisId="spd" domain={[1, 6]} tick={axisStyle} tickCount={6}
              label={{ value: 'speedup (×)', angle: -90, position: 'insideLeft', style: { ...axisStyle, fill: COLORS.teal, fontSize: 12 } }} />
            <YAxis yAxisId="lat" orientation="right" scale="log" domain={[40, 3000]}
              ticks={[50, 100, 300, 1000, 3000]} tick={axisStyle}
              label={{ value: 'single-op GPU latency (µs, log)', angle: 90, position: 'insideRight', style: { ...axisStyle, fill: COLORS.amber, fontSize: 12 } }} />
            <ReferenceLine yAxisId="spd" y={1} stroke={COLORS.gray} strokeDasharray="5 4"
              label={{ value: 'no benefit — offload ≈ per-request work', position: 'insideBottomLeft', style: { fontSize: 10, fill: '#8a92a5' } }} />
            <Tooltip content={
              <Tip render={(p) => {
                const d = p[0].payload
                return (<>
                  <b>{d.label}iB block</b><br />
                  speedup {d.speedup}× · GPU op {d.latencyUs} µs<br />
                  sync {d.sync.toLocaleString()} → async {d.async.toLocaleString()} req/s
                </>)
              }} />
            } />
            <Legend wrapperStyle={{ fontSize: 12 }} />
            <Line yAxisId="spd" type="monotone" dataKey="speedup" name="speedup (async / sync)"
              stroke={COLORS.teal} strokeWidth={2.6} dot={{ r: 4, fill: COLORS.teal }} animationDuration={900} />
            <Line yAxisId="lat" type="monotone" dataKey="latencyUs" name="single-op GPU latency (µs)"
              stroke={COLORS.amber} strokeWidth={2} strokeDasharray="6 4" dot={{ r: 3.5, fill: COLORS.amber }} animationDuration={900} />
          </ComposedChart>
        ) : (
          <ComposedChart data={BLOCKSIZE} margin={{ top: 8, right: 12, bottom: 6, left: 6 }}>
            <CartesianGrid stroke={gridStroke} strokeDasharray="3 3" />
            <XAxis dataKey="label" tick={axisStyle}
              label={{ value: 'AES block size (offload weight)', position: 'insideBottom', offset: -4, style: { ...axisStyle, fontSize: 12 } }} />
            <YAxis tick={axisStyle}
              label={{ value: 'requests / second', angle: -90, position: 'insideLeft', style: { ...axisStyle, fontSize: 12 } }} />
            <Tooltip content={
              <Tip render={(p) => {
                const d = p[0].payload
                return (<>
                  <b>{d.label}iB block</b><br />
                  sync {d.sync.toLocaleString()} req/s<br />
                  async {d.async.toLocaleString()} req/s ({d.speedup}×)
                </>)
              }} />
            } />
            <Legend wrapperStyle={{ fontSize: 12 }} />
            <Bar dataKey="sync" name="synchronous (blocks the loop)" fill={COLORS.red} radius={[4, 4, 0, 0]} animationDuration={800} />
            <Bar dataKey="async" name="async (few-line overlap)" fill={COLORS.slate} radius={[4, 4, 0, 0]} animationDuration={800} />
          </ComposedChart>
        )}
      </ResponsiveContainer>
    </ChartCard>
  )
}

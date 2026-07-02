import {
  ResponsiveContainer, BarChart, Bar, XAxis, YAxis,
  CartesianGrid, Tooltip, Cell, LabelList, Legend,
} from 'recharts'
import { LOST_UPDATES, CORRECTNESS_TPUT, COLORS } from '../data'
import { ChartCard, Tip, axisStyle, gridStroke } from './common'

export function LostUpdatesChart() {
  return (
    <ChartCard
      title="Unprotected overlap silently loses updates"
      sub="Stock Redis, module command: read a key → real GPU offload → write the incremented value back."
      footer="The detector write-protects shared-state pages, snapshots a version clock when a handler parks at its offload, and flags 26,185 conflicts — driving lost updates to zero."
    >
      <ResponsiveContainer width="100%" height={300}>
        <BarChart data={LOST_UPDATES} margin={{ top: 26, right: 12, bottom: 4, left: 6 }}>
          <CartesianGrid stroke={gridStroke} strokeDasharray="3 3" vertical={false} />
          <XAxis dataKey="name" tick={{ ...axisStyle, fontWeight: 600, fill: '#1a1a2e' }} />
          <YAxis tick={axisStyle}
            label={{ value: 'lost updates', angle: -90, position: 'insideLeft', style: { ...axisStyle, fontSize: 12 } }} />
          <Tooltip cursor={{ fill: 'rgba(52,80,127,0.05)' }} content={
            <Tip render={(p) => (<><b>{p[0].payload.name}</b><br />{p[0].value.toLocaleString()} lost updates</>)} />
          } />
          <Bar dataKey="lost" barSize={90} radius={[8, 8, 0, 0]} animationDuration={900}>
            {LOST_UPDATES.map(d => <Cell key={d.name} fill={d.color} />)}
            <LabelList dataKey="lost" position="top"
              formatter={(v) => (v === 0 ? '0 — correct' : `${v.toLocaleString()} lost`)}
              style={{ fontSize: 13, fontWeight: 800, fill: '#1a1a2e' }} />
          </Bar>
        </BarChart>
      </ResponsiveContainer>
    </ChartCard>
  )
}

export function DetectorThroughputChart() {
  return (
    <ChartCard
      title="…and the detector keeps the offloads overlapped"
      sub="Throughput on the same workload: naive (unsafe), the detector, and a coarse lock."
      footer="A coarse lock serializes the very offloads it protects (13.8 K req/s). The detector serializes only conflicting handlers: 24.7 K req/s — 1.8× faster than the lock and within measurement noise of unprotected overlap."
    >
      <ResponsiveContainer width="100%" height={300}>
        <BarChart data={CORRECTNESS_TPUT} margin={{ top: 8, right: 12, bottom: 4, left: 6 }} barGap={4}>
          <CartesianGrid stroke={gridStroke} strokeDasharray="3 3" vertical={false} />
          <XAxis dataKey="name" tick={{ ...axisStyle, fontWeight: 600, fill: '#1a1a2e' }} />
          <YAxis domain={[0, 30]} tick={axisStyle}
            label={{ value: 'throughput (K req/s)', angle: -90, position: 'insideLeft', style: { ...axisStyle, fontSize: 12 } }} />
          <Tooltip cursor={{ fill: 'rgba(52,80,127,0.05)' }} content={
            <Tip render={(p) => (<>
              <b>{p[0].payload.name}</b><br />
              {p.map(s => <span key={s.dataKey}>{s.name}: {s.value} K req/s<br /></span>)}
            </>)} />
          } />
          <Legend wrapperStyle={{ fontSize: 12 }} />
          <Bar dataKey="naive" name="naive (unsafe)" fill={COLORS.gray} radius={[5, 5, 0, 0]} animationDuration={800} />
          <Bar dataKey="detector" name="detector (overlapped)" fill={COLORS.slate} radius={[5, 5, 0, 0]} animationDuration={800} />
          <Bar dataKey="lock" name="coarse lock (serialized)" fill={COLORS.amber} radius={[5, 5, 0, 0]} animationDuration={800} />
        </BarChart>
      </ResponsiveContainer>
    </ChartCard>
  )
}

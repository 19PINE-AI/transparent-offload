import {
  ResponsiveContainer, BarChart, Bar, XAxis, YAxis, CartesianGrid,
  Cell, LabelList, Tooltip, ReferenceLine,
} from 'recharts'
import { HEADLINE, MODELS, COLORS } from '../data'
import { ChartCard, ModelLegend, Tip, axisStyle, gridStroke } from './common'

export default function HeadlineChart() {
  return (
    <ChartCard
      title="Prediction 1, validated: the reroute recovers the win the model predicts in every regime"
      sub="Real GPU, 1 MiB AES offload, idle device. Every integration: no failed requests."
      footer="The cluster tops out at 2–3.5× because this offload is bandwidth-bound: overlap fills the device but cannot exceed its throughput (at 8 MiB blocks the same reroute reaches 5.4×). Thread and goroutine pools reach 3.0–3.5× with no asynchronous code. The databases show only the predicted intra-query win with a launch-bound op (MariaDB ~1.9×)."
    >
      <ResponsiveContainer width="100%" height={380}>
        <BarChart data={HEADLINE} layout="vertical" margin={{ top: 4, right: 52, left: 8, bottom: 4 }}>
          <CartesianGrid horizontal={false} stroke={gridStroke} strokeDasharray="3 3" />
          <XAxis type="number" domain={[0, 4]} tick={axisStyle} tickCount={5}
            label={{ value: 'speedup (×)', position: 'insideBottomRight', offset: -2, style: { ...axisStyle, fontSize: 11 } }} />
          <YAxis type="category" dataKey="app" width={86} tick={{ ...axisStyle, fontWeight: 600, fill: '#1a1a2e' }} />
          <ReferenceLine x={1} stroke={COLORS.gray} strokeDasharray="5 4"
            label={{ value: 'no gain', position: 'insideTopLeft', style: { fontSize: 10, fill: '#8a92a5' } }} />
          <Tooltip cursor={{ fill: 'rgba(52,80,127,0.05)' }} content={
            <Tip render={(p) => {
              const d = p[0].payload
              return (<>
                <b>{d.app}</b><br />
                {d.speedup}× over synchronous<br />
                {d.lines} lines added · {MODELS[d.model].label}
              </>)
            }} />
          } />
          <Bar dataKey="speedup" radius={[0, 6, 6, 0]} barSize={22} isAnimationActive animationDuration={900}>
            {HEADLINE.map(d => <Cell key={d.app} fill={MODELS[d.model].color} />)}
            <LabelList position="right"
              content={({ x, y, width, height, index }) => {
                const d = HEADLINE[index]
                if (!d) return null
                return (
                  <text x={x + width + 6} y={y + height / 2 + 4} fontSize="12" fontWeight="700" fill="#1a1a2e">
                    {d.approx ? `~${d.speedup.toFixed(1)}×` : `${d.speedup.toFixed(2)}×`}{d.dagger ? ' †' : ''}
                  </text>
                )
              }} />
          </Bar>
        </BarChart>
      </ResponsiveContainer>
      <ModelLegend />
    </ChartCard>
  )
}

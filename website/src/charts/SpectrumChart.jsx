import {
  ResponsiveContainer, ScatterChart, Scatter, XAxis, YAxis,
  CartesianGrid, Tooltip, ReferenceLine, Cell, LabelList,
} from 'recharts'
import { SPECTRUM, TRANSPARENT_POINT, MODELS, COLORS } from '../data'
import { ChartCard, ModelLegend, Tip, axisStyle, gridStroke } from './common'

// Per-point label offsets (from the paper's figure) so labels never collide.
const LABEL_POS = {
  Redis: [10, -2, 'start'], 'Node.js': [10, 2, 'start'], Python: [-10, -6, 'end'],
  nginx: [-10, 4, 'end'], memcached: [0, -12, 'middle'], Go: [-10, -4, 'end'],
  Apache: [0, -12, 'middle'], Postgres: [0, 22, 'middle'], MariaDB: [10, 6, 'start'],
  HAProxy: [0, 22, 'middle'],
}

function PointLabel(props) {
  const { x, y, index } = props
  const d = SPECTRUM[index]
  if (!d) return null
  const [dx, dy, anchor] = LABEL_POS[d.app] || [0, -12, 'middle']
  return (
    <text x={x + dx} y={y + dy + 4} textAnchor={anchor}
      fontSize="11" fontWeight="700" fill="#3d4358">
      {d.app}
    </text>
  )
}

function Star({ cx, cy }) {
  if (cx == null) return null
  const pts = []
  for (let i = 0; i < 10; i++) {
    const r = i % 2 === 0 ? 13 : 5.5
    const a = -Math.PI / 2 + (i * Math.PI) / 5
    pts.push(`${cx + r * Math.cos(a)},${cy + r * Math.sin(a)}`)
  }
  return <polygon points={pts.join(' ')} fill="#fff" stroke={COLORS.slate} strokeWidth="2" />
}

export default function SpectrumChart() {
  return (
    <ChartCard
      title="The study in one plot: code cost vs. measured win"
      sub="Lines added (log) vs. speedup from rerouting a real-GPU AES offload on an idle device (log); 1 MiB blocks, databases pipeline a launch-bound op intra-query."
      footer="The star is the zero-edit limit: the transparent runtime on an unmodified thread-per-connection binary, 17.3× — in its niche. The few-line reroutes (22–138 lines, at most one existing line modified) cluster at 2–3.5× across every concurrency model. HAProxy's 138 lines live in a standalone C agent; the proxy itself changes only configuration."
    >
      <ResponsiveContainer width="100%" height={400}>
        <ScatterChart margin={{ top: 18, right: 30, bottom: 14, left: 6 }}>
          <CartesianGrid stroke={gridStroke} strokeDasharray="3 3" />
          <XAxis
            type="number" dataKey="x" scale="log" domain={[1, 250]}
            ticks={[1, 10, 100]} tick={axisStyle}
            tickFormatter={(v) => (v === 1 ? '~0' : String(v))}
            label={{ value: 'lines added to the application', position: 'insideBottom', offset: -6, style: { ...axisStyle, fontSize: 12 } }}
          />
          <YAxis
            type="number" dataKey="y" scale="log" domain={[0.9, 25]}
            ticks={[1, 2, 3, 5, 10, 20]} tick={axisStyle}
            label={{ value: 'speedup (×)', angle: -90, position: 'insideLeft', style: { ...axisStyle, fontSize: 12 } }}
          />
          <ReferenceLine y={1} stroke={COLORS.gray} strokeDasharray="5 4"
            label={{ value: 'no gain', position: 'insideBottomRight', style: { fontSize: 10, fill: '#8a92a5' } }} />
          <Tooltip cursor={{ strokeDasharray: '4 4' }} content={
            <Tip render={(p) => {
              const d = p[0].payload
              return (<>
                <b>{d.app}</b><br />
                {d.lines === 0 ? 'zero source modification (LD_PRELOAD)' : `${d.lines} lines added`}<br />
                {d.y}× speedup{d.model ? ` · ${MODELS[d.model].label}` : ''}
              </>)
            }} />
          } />
          <Scatter data={SPECTRUM} isAnimationActive animationDuration={800}>
            {SPECTRUM.map(d => <Cell key={d.app} fill={MODELS[d.model].color} stroke="#fff" strokeWidth={1.5} r={9} />)}
            <LabelList dataKey="app" content={<PointLabel />} />
          </Scatter>
          <Scatter data={[TRANSPARENT_POINT]} shape={<Star />}>
            <LabelList dataKey="app" position="right" offset={14}
              style={{ fontSize: 11, fontWeight: 700, fill: COLORS.slate }} />
          </Scatter>
        </ScatterChart>
      </ResponsiveContainer>
      <ModelLegend />
    </ChartCard>
  )
}

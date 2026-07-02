import { MODELS } from '../data'

export function ChartCard({ title, sub, children, footer }) {
  return (
    <div className="panel">
      <div className="chart-title">{title}</div>
      {sub && <div className="chart-sub">{sub}</div>}
      {children}
      {footer && <div className="note">{footer}</div>}
    </div>
  )
}

export function ModelLegend({ keys }) {
  const ks = keys || Object.keys(MODELS)
  return (
    <div className="legend-row">
      {ks.map(k => (
        <div className="legend-item" key={k}>
          <span className="legend-swatch" style={{ background: MODELS[k].color }} />
          {MODELS[k].label}
        </div>
      ))}
    </div>
  )
}

export function Tip({ active, payload, render }) {
  if (!active || !payload || !payload.length) return null
  return <div className="tooltip-box">{render(payload)}</div>
}

export const axisStyle = {
  fontSize: 11.5,
  fill: '#5b6577',
  fontFamily: 'Inter, sans-serif',
}

export const gridStroke = '#e6e3da'

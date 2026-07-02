import { REGIMES } from '../data'
import Reveal from './Reveal'

export default function RegimeCards() {
  return (
    <div className="regimes">
      {REGIMES.map((r, i) => (
        <Reveal key={r.title} delay={i * 80}>
          <div className="regime">
            <div className="regime-head" style={{ background: r.color }}>
              <h4>{r.title}</h4>
              <p>{r.cost}</p>
            </div>
            <div className="regime-body">
              <div className="regime-win" style={{ color: r.color }}>{r.win}</div>
              <div className="regime-note">{r.note}</div>
              <div className="regime-note" style={{ marginTop: 8 }}>
                <strong>suspension primitive:</strong> {r.primitive}
              </div>
              <div className="regime-ex">{r.examples}</div>
            </div>
          </div>
        </Reveal>
      ))}
    </div>
  )
}

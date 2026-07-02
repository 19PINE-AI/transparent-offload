import { PATTERNS } from '../data'
import Reveal from './Reveal'

export default function PatternCards() {
  return (
    <>
      <div className="patterns">
        {PATTERNS.map((p, i) => (
          <Reveal key={p.title} delay={i * 90}>
            <div className={`pattern ${p.safe ? 'pattern-safe' : 'pattern-hazard'}`}>
              <div className="pattern-verdict">{p.safe ? '✓ SAFE' : '✗ HAZARDOUS'}</div>
              <h4>{p.title}</h4>
              <div className="pattern-ex">{p.examples}</div>
              <p>{p.why}</p>
              <div className="pattern-result">{p.result}</div>
            </div>
          </Reveal>
        ))}
      </div>
      <Reveal>
        <p className="note" style={{ marginTop: 16 }}>
          The dividing line is the <strong>state pattern, not the application domain</strong>: bulk crypto,
          compression, hashing, and stateless inference land safe because the heavy routine is pure and
          their shared state is read-only or already locked. Measured under overlapped execution in
          race-instrumented harnesses spanning crypto (OpenSSL) and compression (zlib).
        </p>
      </Reveal>
    </>
  )
}

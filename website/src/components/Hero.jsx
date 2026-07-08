import { LINKS } from '../data'
import Reveal from './Reveal'

const stats = [
  { value: '1.2–5.4×', label: 'from rerouting the offload through each server’s own concurrency — ten servers, real hardware' },
  { value: '22–138', label: 'lines added per server, at most one existing line modified — predictable in advance' },
  { value: '17.3×', label: 'at the zero-edit limit: an LD_PRELOAD fiber runtime on an unmodified binary, real GPU AES' },
  { value: '0', label: 'lost updates with the transparent conflict detector guarding the one hazardous state pattern' },
]

export default function Hero() {
  return (
    <header className="hero" id="top">
      <div className="container">
        <Reveal>
          <div className="hero-kicker">● &nbsp;Systems research · measured on real hardware</div>
          <h1>Fine&#8209;Grained Computation Offload for Off&#8209;the&#8209;Shelf Servers in Tens of Lines</h1>
          <p className="hero-question">
            What should the CPU do while it waits for the accelerator? It should overlap the offload
            with other requests — and it doesn’t need a new framework, runtime, or OS to do so, because
            every server that serves concurrent requests already ships the machinery overlap requires.
            Hiding a fine-grained offload is a routing problem, not a rewrite problem.
          </p>
          <p className="hero-authors">
            <strong>Bojie Li</strong> · Pine AI
          </p>
          <div className="hero-actions">
            <a className="btn btn-primary" href={LINKS.arxiv} target="_blank" rel="noreferrer">Read on arXiv</a>
            <a className="btn btn-ghost" href={LINKS.code} target="_blank" rel="noreferrer">Code on GitHub</a>
            <a className="btn btn-ghost" href="#results">Jump to results ↓</a>
          </div>
        </Reveal>
        <div className="hero-stats">
          {stats.map((s, i) => (
            <Reveal key={s.value} delay={i * 90}>
              <div className="stat-card">
                <div className="stat-value">{s.value}</div>
                <div className="stat-label">{s.label}</div>
              </div>
            </Reveal>
          ))}
        </div>
      </div>
    </header>
  )
}

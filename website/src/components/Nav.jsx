import { LINKS } from '../data'

const items = [
  ['#problem', 'Motivation'],
  ['#model', 'Model'],
  ['#recipe', 'Method'],
  ['#transparent', 'Zero-Edit Limit'],
  ['#envelope', 'Envelope'],
  ['#correctness', 'Correctness'],
  ['#results', 'Evaluation'],
]

export default function Nav() {
  return (
    <nav className="nav">
      <div className="nav-inner">
        <a className="nav-brand" href="#top">transparent-offload</a>
        <div className="nav-links">
          {items.map(([href, label]) => (
            <a key={href} href={href}>{label}</a>
          ))}
        </div>
        <a className="nav-cta" href={LINKS.arxiv} target="_blank" rel="noreferrer">arXiv</a>
      </div>
    </nav>
  )
}

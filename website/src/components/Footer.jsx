import { BIBTEX, LINKS } from '../data'

export default function Footer() {
  return (
    <footer className="footer">
      <div className="container">
        <h3>Cite this work</h3>
        <p>
          Paper: <a href={LINKS.pdf} target="_blank" rel="noreferrer">PDF</a> ·
          Code &amp; artifacts: <a href={LINKS.code} target="_blank" rel="noreferrer">github.com/19PINE-AI/transparent-offload</a> ·
          Project page: <a href={LINKS.site} target="_blank" rel="noreferrer">01.me/research/transparent-offload</a>
        </p>
        <div className="bibtex">{BIBTEX}</div>
        <div className="footer-meta">
          All results measured on real hardware (NVIDIA RTX PRO 6000, Blackwell) with no emulated latencies —
          except the open-loop latency sweep, which uses a controlled 20 µs emulated offload and is labeled as
          such. This page’s charts are rendered from the paper’s measurement data.
        </div>
      </div>
    </footer>
  )
}

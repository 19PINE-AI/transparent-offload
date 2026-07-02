import Reveal from './Reveal'

export default function Section({ id, kicker, title, lede, accent, children }) {
  return (
    <section id={id} style={accent ? { '--accent': accent } : undefined}>
      <div className="container">
        <Reveal>
          <div className="section-kicker">{kicker}</div>
          <h2>{title}</h2>
          {lede && <p className="lede">{lede}</p>}
        </Reveal>
        {children}
      </div>
    </section>
  )
}

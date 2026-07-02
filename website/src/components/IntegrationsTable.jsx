import { INTEGRATIONS, HEADLINE, MODELS } from '../data'
import Reveal from './Reveal'

const modelOf = Object.fromEntries(
  HEADLINE.map(d => [d.app === 'Postgres' ? 'PostgreSQL' : d.app, d.model])
)

export default function IntegrationsTable() {
  return (
    <Reveal>
      <div className="table-wrap">
        <table className="data">
          <thead>
            <tr>
              <th>Server</th>
              <th>Concurrency model</th>
              <th>Integration point</th>
              <th>+ Lines</th>
              <th>Modified</th>
              <th>Speedup</th>
            </tr>
          </thead>
          <tbody>
            {INTEGRATIONS.map(row => {
              const model = MODELS[modelOf[row.app]]
              return (
                <tr key={row.app}>
                  <td><strong>{row.app}</strong> <span className="mono" style={{ color: '#8a92a5' }}>{row.version}</span></td>
                  <td>{model && <span className="pill" style={{ background: model.color }}>{model.label}</span>}</td>
                  <td>{row.how}</td>
                  <td className="mono">{row.lines}</td>
                  <td className="mono">{row.mod}</td>
                  <td className="mono"><strong>{row.speedup}</strong></td>
                </tr>
              )
            })}
          </tbody>
        </table>
      </div>
      <p className="note">
        Speedups are over the synchronous offload with a real GPU (1 MiB AES; †the databases pipeline the
        op intra-query: serial vs. pipelined offloads within one query). Seven are stock server binaries; for Node.js, Python, and Go the
        server is an idiomatic handler on the stock runtime. Added lines live in a module or extension and
        survive server upgrades; modified lines are the invasive part, and zero is achievable wherever an
        extension interface exists — nine of the ten. memcached, the one server with no deferred-response
        primitive, is the exception that proves the structural claim: <strong>the cost of the reroute is
        the distance to the server’s nearest suspend/resume primitive</strong>.
      </p>
    </Reveal>
  )
}

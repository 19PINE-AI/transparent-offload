import Reveal from './Reveal'

export default function CodeExample() {
  return (
    <Reveal>
      <div className="grid-2" style={{ alignItems: 'stretch' }}>
        <div>
          <pre className="code">{`/* The reroute in Redis — condensed from the
   83-line module accel_module.c (paper Listing 1).
   BlockClient is Redis's own deferred-response
   primitive; the worker pool is the executor. */

/* accel.async: submit, suspend, resume */
`}<span className="k">int</span>{` cmd_async(RedisModuleCtx *ctx, ...) {
    RedisModuleBlockedClient *bc =
        `}<span className="hl">RedisModule_BlockClient</span>{`(ctx, reply_cb,
             timeout_cb, NULL, 0); `}<span className="c">/* 2. suspend */</span>{`
    enqueue(bc);                  `}<span className="c">/* 1. submit  */</span>{`
    `}<span className="k">return</span>{` REDISMODULE_OK; `}<span className="c">/* loop serves others */</span>{`
}
`}<span className="k">void</span>{` *worker(`}<span className="k">void</span>{` *arg) {        `}<span className="c">/* pool thread */</span>{`
    `}<span className="k">for</span>{` (;;) { job_t j = dequeue();
        accel_encrypt(j.buf, j.len); `}<span className="c">/* offload */</span>{`
        `}<span className="hl">RedisModule_UnblockClient</span>{`(j.bc, NULL);
    }                             `}<span className="c">/* 3. resume */</span>{`
}`}</pre>
        </div>
        <div>
          <pre className="code">{`# Python / asyncio — the whole reroute is one line
`}<span className="c"># before: blocks the entire event loop</span>{`
result = `}<span className="hl">gpu_aes(block)</span>{`

`}<span className="c"># after: the loop keeps serving other requests</span>{`
result = `}<span className="k">await</span>{` loop.`}<span className="hl">run_in_executor</span>{`(pool,
                 gpu_aes, block)


`}<span className="c"># Go needs no asynchronous code at all —
# parking the goroutine IS the routing:</span>{`
func handler(w http.ResponseWriter, r *http.Request) {
    out := C.accel_encrypt(buf)  `}<span className="c">// plain blocking
                                 // cgo call; the
                                 // scheduler overlaps</span>{`
    w.Write(out)
}`}</pre>
        </div>
      </div>
      <p className="note">
        The synchronous variant of the Redis command simply calls <code>accel_encrypt</code> on the
        event-loop thread — <strong>the diff between the two is the reroute</strong>. The core of the
        integration is a dozen lines; nothing in Redis itself changes.
      </p>
    </Reveal>
  )
}

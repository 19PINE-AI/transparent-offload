# Transparent Offload

Code and measurements for the paper **"Fine-Grained Computation Offload for Off-the-Shelf
Servers in Tens of Lines"** (Bojie Li, Pine AI).

What should the CPU do while it waits for a fine-grained accelerator offload (microseconds to a
few milliseconds)? Blocking pays a context switch comparable to the offload itself; busy-waiting
burns the core. The answer is to *overlap* the offload with other requests' work — and this repo
measures **how much an existing server must change to do that**, across ten off-the-shelf servers
and a zero-modification `LD_PRELOAD` fiber runtime, all on real hardware (NVIDIA RTX PRO 6000
Blackwell GPU; a real RSA-2048 TCP signer for the remote-offload class).

**arXiv:** https://arxiv.org/abs/2607.02630 &nbsp;·&nbsp; **Project page:** https://01.me/research/transparent-offload &nbsp;·&nbsp; **Paper PDF:** [`paper/paper.pdf`](paper/paper.pdf)

## Layout

| Directory | Contents |
|---|---|
| `paper/` | LaTeX source, figures (`figs/gen_figs.py`), and the compiled `paper.pdf` |
| `transparent-runtime/` | The `LD_PRELOAD` M:N fiber runtime, its results (`TRANSPARENCY_RESULTS.md`), and the fiberizability classifier (`sweep/`) |
| `transparent-runtime/apps/` | The ten minimal-edit server integrations (Redis, nginx, memcached, Node.js, Python, Apache, Go, PostgreSQL, MariaDB, HAProxy) and `MINIMAL_EDIT_RESULTS.md` |
| `runtime/` | Coroutine-vs-block-vs-busy microbenchmarks and the open-loop latency study (`openloop.csv`) |
| `detector/`, `conflict-measurement/` | The page-protection conflict detector and the shared-state correctness study |
| `gpu/` | GPU AES kernels and latency characterization |
| `transparency/` | Offload-boundary interposition study |

Start with [`INDEX.md`](INDEX.md), which maps every claim in the paper to its evidence file, and
[`EXPERIMENT_PLAN.md`](EXPERIMENT_PLAN.md) for the full experimental plan. Build notes (CUDA
version, scheduling privileges) are at the bottom of `INDEX.md`.

## Citation

If you use this work, please cite the paper:

```bibtex
@misc{transparentoffload,
  author        = {Li, Bojie},
  title         = {Fine-Grained Computation Offload for Off-the-Shelf Servers in Tens of Lines},
  year          = {2026},
  eprint        = {2607.02630},
  archivePrefix = {arXiv},
  url           = {https://arxiv.org/abs/2607.02630},
  note          = {Code and data: https://github.com/19PINE-AI/transparent-offload},
}
```

## License

MIT — see [LICENSE](LICENSE).

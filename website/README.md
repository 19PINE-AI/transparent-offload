# Paper website

A single-page React site presenting *Fine-Grained Computation Offload for
Event-Driven Applications* — animated SVG illustrations of the system
(offload stall vs. overlap, the LD_PRELOAD fiber runtime, the three walls,
the minimal-edit recipe, the shared-state race) and interactive Recharts
charts rendered from the paper's real measurement data (`src/data.js`,
sourced from `paper/figs/gen_figs.py` and `body.tex`).

## Develop

```sh
npm install
npm run dev        # dev server
npm run build      # static site in dist/
npm run preview    # serve dist/
```

The build is fully static (relative asset paths), so `dist/` can be hosted
on GitHub Pages or any static host as-is.

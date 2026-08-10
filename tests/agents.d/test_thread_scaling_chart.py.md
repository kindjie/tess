# test_thread_scaling_chart.py

- `tests/test_thread_scaling_chart.py`: pytest coverage for the
  serial-versus-pool crossover chart (`tools/thread_scaling_chart.py`),
  which emits SVG by hand. It pins well-formed single-root output, no
  external references at all — the docs site must render it offline, so a
  remote font or image would fail silently in the published page, and
  only the xmlns identifier is allowed — one marker per point, the unity
  reference line and the crossover band, the log10 x axis (even decade
  spacing, endpoints on the plot edges, non-positive input rejected),
  empty input rejected, determinism, a `prefers-color-scheme: dark` rule
  so the docs dark mode stays readable, and markup escaped in series
  labels. Reading a sweep artifact divides each width by the serial
  baseline from its own process, keyed by `tess_run_group`: pooling
  baselines across processes is exactly the confounding the paired runs
  exist to remove.

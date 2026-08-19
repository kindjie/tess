# test_branding_assets.py

- `tests/test_branding_assets.py`: static asset and browser-demo contract
  coverage, including backend rejection of occupied wall tiles, separate wall-
  and crowd-blocked outcomes, bounded recovery and whole-wave turnaround,
  movement diagnostics, optional congestion-triggered route spreading, and
  maintained model/native/Wasm seams. It also covers documentation navigation,
  dependencies, licensing, accessibility, readiness, and browser-smoke
  contracts. Assertions intentionally inspect source text so hook-backstop CI
  catches drift without requiring Emscripten or a browser; keep literal checks
  synchronized with the templates and configuration they pin.

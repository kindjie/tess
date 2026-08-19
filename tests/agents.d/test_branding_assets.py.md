# test_branding_assets.py

- `tests/test_branding_assets.py`: static asset and browser-demo contract
  coverage, including the shared native/WebAssembly pathfinding-strategy model,
  checked scalar ABI, accessible comparison embed, backend rejection of
  occupied wall tiles, wall removal, separate wall- and crowd-blocked
  outcomes, bounded recovery and whole-wave turnaround, movement diagnostics,
  optional congestion-triggered route spreading, and maintained
  model/native/Wasm seams. Traffic Lab checks pin its
  compile-time shape, deterministic scenarios, bounded planning, cached
  terrain, diagnostics, real-browser interaction/layout harness, bounded
  browser measurement, scenario-sharded native self-check registration, and
  the exact TSan exemption boundary for serial-only guided checks. The full
  correctness evidence remains blocking in the other compiler and sanitizer
  gates. It also pins the native percentile campaign seam without making
  wall-clock timing authoritative. It covers the comparison article's
  scoped algorithm/strategy status contract, documentation navigation,
  dependencies, licensing, accessibility, readiness, and browser-smoke
  contracts. Assertions intentionally inspect source text so hook-backstop CI
  catches drift without requiring Emscripten or a browser; keep literal checks
  synchronized with the templates and configuration they pin.
- The README identity assertion pins a plain-language H1 naming pathfinding
  and 2D/3D grid worlds, while the opening sentence carries the C++20 term.

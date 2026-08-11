# tess_topology_movement_test

- `tess_topology_movement_test`: pins movement-class and transition-provider
  identity throughout region-graph build, freshness, incremental update, and
  precheck. Incremental graphs must equal full rebuilds for every class and
  provider change; equal revisions do not make distinct live provider
  instances interchangeable. Sparse provider edges degrade to `Indeterminate`,
  never a false `Unreachable`. Sideways cross-chunk stairs emit both directions,
  while a landing crossing two chunk boundaries contributes nothing.

# tess_path_movement_class_test

- `tess_path_movement_class_test`: pins movement-class parity with raw tags
  across unit, weighted, field, cache, runtime, agent, and provider-aware paths.
  The central contract is plan equals commit: every accepted step validates as
  `Moved` under the same class, while endpoint failures remain class-specific.
  Missing provider topology reports `StaleTopology` and outranks a blocked
  regular edge; an unavailable transition between passable endpoints reports
  `Blocked`, while a valid regular edge bypasses provider enumeration. Sparse
  uncertainty remains `Indeterminate`.
  Runtime and direct route caches bind provider type, live instance, revision,
  scale, and movement class so alternating callers cannot receive one another's
  routes. Exact unrepresentable costs report `CostOverflow`; a provider
  exception cannot turn a partially built distance field into `NoPath`.
  Unit and weighted field readers report `NotComputed` when changed world data
  leaves no valid descent through an otherwise finite field gradient.
  Provider-aware weighted search retains its five-argument function type while
  exposing seeded ties as a separate overload.

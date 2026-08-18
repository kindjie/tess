# tess_path_movement_class_test

- `tess_path_movement_class_test`: pins movement-class parity with legacy tags
  across unit, weighted, field, cache, runtime, agent, and provider-aware paths.
  The central contract is plan equals commit: every accepted step validates as
  `Moved` under the same class, while endpoint failures remain class-specific.
  Provider topology outranks a blocked regular edge, but a valid regular edge
  bypasses provider enumeration; sparse uncertainty remains `Indeterminate`.
  Runtime and direct route caches bind provider type, live instance, revision,
  scale, and movement class so alternating callers cannot receive one another's
  routes. Exact unrepresentable costs report `CostOverflow`.
  Provider-aware weighted search retains its five-argument function type while
  exposing seeded ties as a separate overload.

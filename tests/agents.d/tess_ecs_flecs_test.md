# tess_ecs_flecs_test

- `tess_ecs_flecs_test`: verifies the optional Flecs adapter against the
  pinned real Flecs release: 64-bit generation-preserving handle conversion,
  exclusion of the all-ones tess null sentinel, immediate-mode lifecycle
  enforcement across lifecycle intents, collection, write-back, and tick
  drivers; context/world identity enforcement; transactional occupancy-index
  growth for spawn/place,
  deterministic collection under table churn, goal reconciliation, safe
  two-phase write-back, synchronized spawn/move/park/place/despawn lifecycle
  operations, render deltas, and allocation-free warm ticks.

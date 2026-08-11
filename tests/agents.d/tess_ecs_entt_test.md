# tess_ecs_entt_test

- `tess_ecs_entt_test` (built only with `TESS_ENABLE_ENTT`; links real EnTT at
  the pinned SHA): pins adapter handles, lifecycle, goals, determinism,
  synchronization, failure atomicity, and allocation-free warm operation.
  Every tick runs both per-entity agreement and the exhaustive all-tiles
  occupancy biconditional. Create/destroy churn deliberately changes pool
  packing while `AgentId`-ordered outcomes and statistics remain identical.

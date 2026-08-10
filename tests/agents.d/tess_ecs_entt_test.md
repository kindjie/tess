# tess_ecs_entt_test

- `tess_ecs_entt_test` (built only with `TESS_ENABLE_ENTT`; links real
  EnTT at the pinned SHA): verifies the EnTT adapter (M10): null-handle
  conversion special-cased both directions plus live-entity round-trips;
  spawn claiming field + index and refusing occupied/out-of-bounds tiles
  with monotonic ids across refusals; allocation-failure preflight leaving
  spawn/place ECS, world, index, and ID state unchanged; agents walking to
  goals with the
  full section-8 sync sweep (per-entity position/index/field agreement
  AND the exhaustive all-tiles biconditional) after every tick; identical
  per-tick stats and positions across two registries whose pool packing
  diverges via create/destroy churn (AgentId-order determinism); tickets
  surviving quiet ticks with reprocessing only on re-arm (mid-flight
  reassignment repaths without teleporting); equidistant two-agent
  contention keeping the index injective with the earlier spawn winning
  and the loser waiting Blocked; unreachable goals staying terminal with
  the retained-but-inert `PathGoal` until a new goal revives the agent;
  teleport retaining the goal (re-arm from the new position; teleport
  onto the goal arriving via the submit-time path; occupied destinations
  refused); despawn freeing its tile while other agents' tickets stay
  valid; the park/place board-edge lifecycle (parked agents excluded,
  double-park and parked-teleport refused, off-board spawn placeable);
  and an allocation-free steady state.

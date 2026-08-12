# tess_delta_frame_lifetime_test

- `tess_delta_frame_lifetime_test`: pins the `DeltaFrame` lifetime
  contract (its header comment stated it incorrectly) and
  `DeltaCollector`'s move semantics. A published frame survives
  `begin_tick`, `record_*`, `collect_*` and `clear()`, all of which touch
  only the pending buffers; records carry distinct values and the entity
  is checked, so a record replaced by its neighbour fails. The collector
  asserts all four special-member traits, that a moved-from collector's
  next publish is forced truncated so its consumer resyncs, that the move
  destination is NOT poisoned, and that assigning a fresh collector clears
  the poison. Injected failures into move construction and assignment prove
  invalidation and moved-from poisoning precede the only throwing allocation,
  so a failed move cannot leave a live frame pointing into transferred
  storage.
  Death tests prove publication, reserve, move, and collector destruction
  invalidate every frame accessor before it can expose stale storage.
  `header` remains a value member and intentionally survives invalidation.

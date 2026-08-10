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
  the poison.
  Deliberately uncovered: a bare `published_chunks_.clear()` leaves the
  bytes of trivial elements in place, so reads through the span still
  return the old values and no outside assertion distinguishes it. Also no
  `reserve()` test (reallocation is not observable through a span) and no
  `header` test (a value member of the caller's own frame, so an assertion
  compares a copy against itself).

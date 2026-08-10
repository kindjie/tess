# tess_dirty_property_test

- `tess_dirty_property_test`: seeded deferred-dirty sequences
  (redesign section 3.4, phase 7 slice b-iii). Split out of the
  planning model deliberately: planning creates NO dirty records, so a
  merge, a zero-mask rejection and a coalesced apply are all
  unreachable from an enqueue-and-plan sequence. Drives the
  accumulator and partitions directly.

  Asserts that a zero mask is never recorded (mask index 0 is zero on
  purpose — nothing else reaches that rule), that an out-of-range chunk
  is rejected without recording, that a merge reports the number of
  DISTINCT chunks rather than the record count, that a successful merge
  clears the accumulator, and that collection conserves records.

  `collect_planned_dirty` moves records FROM the partitions INTO the
  accumulator. The first draft had that backwards; the harness caught
  it and shrank to the two-operation sequence `15,51`. Fewer chunks
  than records is what makes coalescing reachable, and the gate
  requires a merge that actually coalesced — a merge where every record
  hit a distinct chunk would satisfy "a merge happened" while leaving
  the coalescing rule untested.

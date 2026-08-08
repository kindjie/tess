- Queued-operation planning no longer scales quadratically with the
  operation count on per-chunk workloads. Hazard detection and
  parallel-phase grouping both compared every candidate against every
  operation accepted so far, which cost the most on the ordinary
  per-chunk-edit shape where the operations are pairwise disjoint and
  every comparison fails. Both now consult a chunk-keyed index and examine
  only the operations that actually share a chunk. Measured on an Apple
  M3 Max, planning a 256-chunk frame went from 58.4 us to 12.0 us and a
  4096-chunk frame from 22.99 ms to 208 us; the scaling matters more than
  either figure, since 16x the operations used to cost 394x the time and
  now costs 17.4x.
- Planning results are unchanged, including which operation a hazard
  blames, and `plan_operations` into a caller-owned report stays
  allocation-free in the steady state.
- The index is bounded, because it is the wrong structure for wide
  domains: an operation covering a whole domain would store one entry per
  chunk and make every later lookup walk one chain per chunk, where the
  scan it replaced rejected non-hazarding pairs on the field mask alone.
  Operations wider than 64 chunks therefore stay out of the index and are
  scanned as before. Whole-domain workloads — `resident_chunks()` is the
  default selector — measure about 10% slower than before this change, the
  cost of the bound on a shape that gains nothing from the index; the new
  `queued/plan_frame_dense_64` benchmark gates that number so it cannot
  drift.
- Plans under sixteen operations keep the all-pairs comparison, since
  `plan_parallel_execution_phases` must build its index per call and two
  allocations are not repaid by a handful of comparisons.

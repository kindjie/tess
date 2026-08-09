- Four unmeasured paths now have benchmarks, so a change to them moves a
  number. `collect_render_tile_deltas` — which the scheduler calls every
  tick — had none at all, and the existing `render_delta/*` family
  exercises a different collector; the new pair holds the chunk count
  fixed and varies only the dirty count, showing that 256x the dirty work
  costs 2.2x the time because the full chunk sweep dominates.
  `spatial/local_coordination` gains a second size, because its cost is
  quadratic in the reservation count and one fixed size cannot separate a
  constant-factor regression from an exponent change; 4x the requests cost
  8.2x the time. The `fields/*` product family gains a 512x512 world, its
  first memory-bound point — every other cell fits in L1/L2, so layout and
  per-build allocation changes were measured only where memory traffic is
  free.
- The paired sentinel representing `include/tess/sim/` measured thread
  creation rather than the code that directory owns, and was the widest
  interval of the twelve sentinels; it is replaced by a compute-bound cell.
- `RouteCacheScratch` no longer copies its whole suffix slot table before
  reassigning it. No measurable effect on the owning benchmark family, which
  is recorded as such rather than presented as a win.

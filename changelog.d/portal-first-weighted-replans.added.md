- Opt-in portal-first serving for single-goal weighted replans:
  `PathRuntimeCachePolicy::weighted_replan_strategy =
  WeightedReplanStrategy::PortalFirst` routes eligible singletons (dense
  worlds, default adjacent transitions, legacy weighted tag classes)
  through a chunk-portal route stitched via the runtime's segment cache
  before falling back to exact A*. Accepted routes are legal and
  verified but may exceed the optimal cost, bounded by a premium cap
  (default 4/3 of the Manhattan lower bound, so at most 4/3 of optimal);
  every other outcome — no candidate, a failed segment, a cap rejection,
  or an ineligible request — is served by exact A* with byte-identical
  results, and per-outcome statistics are reported. The default strategy
  is unchanged. A new cache-aware builder,
  `build_weighted_chunk_portal_route_product_cached`, exposes the same
  composition to direct callers. On the goal-churn benchmark map the
  repeated-churn tick drops from ~18.8 ms to ~24 us on the calibration
  machine; fresh non-repeating goals measure net-neutral under the
  default cap, and the forced worst case (every attempt rejected) costs
  about 2% over exact — all three shapes carry their own cells.

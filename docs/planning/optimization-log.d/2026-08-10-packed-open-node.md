## 2026-08-10 - One compare orders the weighted open lists

- Area: every weighted open list — the weighted A* loop (astar.h), the
  weighted distance-field flood (path.h), the boxed flood
  (distance_field_box.h), and both field-product flood loops
  (field_product_cache.h). Follow-up to the banked Deck main-suite
  baseline (2026-08-08), whose largest absolute costs are the weighted
  A* batch cells (1.21 s / 1.08 s / 0.55 s on device), and to the
  DWARF srcline finding of ~18.5% stl_heap push/pop plus ~19%
  comparator/iterator inside exact weighted A* — measured on the
  goal-churn singleton profile; transfer to the batch cells was a
  hypothesis this change's A/B tested.
- Hypothesis: the three-field comparator (f asc, g desc, index asc)
  costs up to three compare-branches per heap step; concatenating f
  and UINT32_MAX - g into one 64-bit key decides almost every step
  with one compare, without changing which node pops.
- Method: `detail::PackedOpenNode{key, index}` replaces the element of
  the private open-list vectors in place (same 16-byte size, same
  reserve/clear lifecycle, no public type changed). The ordering is a
  strict total order (index breaks every remaining tie; same-index entries
  differ in g by the strict-improvement push guard), and the key is
  injective and order-isomorphic to the old comparator's first two
  fields over their FULL range — UINT32_MAX - g is defined and
  invertible for every g — so any correct heap pops the identical
  sequence and behavior is preserved by construction. The unit-cost
  two-bucket loop keeps its dial semantics through f()/g() accessors.
  Prior art honored: the 2026-06-05 comparator/OpenNode experiments
  (by-const& comparator, Coord3 payload — both rejected) recorded
  "reconsider if the open-set representation changes"; this is that
  change, and the tie-break ORDER those experiments settled is
  preserved bit-exactly.
- Evidence: accepted on M3 (interleaved A/B/A/B, 2 repetitions per
  round, rounds within 1.3%): weighted_astar_batch mixed 568.4 to
  491.9 ms (-13.5%), shared_sparse 271.6 to 233.5 ms (-14.0%),
  multigoal_sparse 515.4 to 446.3 ms (-13.4%),
  weighted_distance_field batch 135.5 to 120.1 ms (-11.3%) — the
  flood gain arriving despite the review's expectation that floods
  (which push f == g) would benefit less. Bounded flood and
  goal_churn_portal watch cells flat. The bench counters double as an
  equivalence check: expanded_total and cost_total are identical
  before and after. Steam Deck verification pending device access;
  the platform baselines differ (libc++ heap vs libstdc++), so the M3
  result does not transfer either direction.
- Equivalence: comparator-isomorphism tests over corner and seeded
  triples, pack round trips at all corner values, a differential heap
  test pinning identical pop sequences on adversarial insertions
  (same-index stale chains, equal-(f,g) index ties, boundary words),
  and tie-heavy goldens captured from the pre-change loops for every
  packed consumer: weighted A* (cost, path length and walk validity,
  expanded/reached literals), the general flood (expanded/reached and
  replay-cost literals), the boxed flood, the weighted goal-set
  product, and the unit-product weighted branch forced by a
  stair-transitions provider (expanded/reached literals each). The
  originally drafted bounded-flood case was dropped after review
  showed that consumer routes to the bucket implementation and never
  touches the packed node. Scratch-reuse pins across
  weighted/unit/weighted searches and warm allocation-freedom close
  the lifecycle. Fail-before mutant: dropping the packed index
  tie-break fails 6 tests.
- Follow-ups: a d-ary sift over the same packed element, only if the
  next profile still shows the heap hot; Deck on-device verification
  when access returns.

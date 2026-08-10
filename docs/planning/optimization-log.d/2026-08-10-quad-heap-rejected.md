## 2026-08-10 - Four-ary sift: rejected on both platforms

- Area: the packed open-list heap (all weighted searches). Follow-up
  to the Deck verification entry, which measured ~20% of the mixed
  batch cell still in libstdc++ sift machinery and named a d-ary sift
  the justified next experiment.
- Hypothesis: halving sift levels (arity 4 over the same strict total
  order — pop sequence provably unchanged, pinned by the differential
  test driving the new functions against a std-heap oracle plus the
  per-consumer goldens) would recover part of the Deck's ~20%.
- Method: measurement-first under a reviewer-validated protocol
  (predeclared endpoints: primary = the profiled mixed batch cell,
  secondary = geomean of the three A* batch cells; ship threshold
  Deck >= 5%, M3 non-regression within 2%; balanced ABBA/BAAB blocks,
  8 invocations per binary per platform; binary SHAs recorded; commits
  9d0d6a3 and the bottom-first follow-up on branch perf/quad-heap,
  never merged). Two variants: the classic early-exit hole sift, then
  the bottom-first sift (libstdc++'s strategy at arity 4) after the
  first variant's flood regressions were diagnosed as early-exit
  overhead on pops that sink a recent push to the bottom.
- Evidence: REJECTED, decisively and symmetrically.
  - Variant 1 (early-exit): Deck A* batches -4.8/-5.1% but mixed only
    -1.3%, flood batch +4.8%, product-build flood +30.3%; M3 +9.2 to
    +14.4% on every weighted cell.
  - Variant 2 (bottom-first): Deck A* -5.3/-5.3/-3.1% (secondary
    geomean -4.6%), flood batch +2.1%, product-build flood +21.8%; M3
    +7.4 to +16.1% on every weighted cell.
  - Both variants fail the primary threshold, regress a covered
    consumer badly on the Deck, and fail M3 non-regression outright.
    The standard-library heaps beat this hand-rolled arity-4
    implementation on every flood workload on both platforms and on
    everything on M3; the only sustained gain is ~5% on two Deck A*
    batch cells.
- Interpretation, scoped per the protocol review: this rejects "the
  4-ary implementation", not arity as a concept — the comparison
  bundles arity with sift strategy, inlining, and codegen. The
  libstdc++ ~20% sift share on the Deck is real but is evidently near
  the cost floor for this access pattern; share is not headroom.
- Retry conditions: only with a structurally different open set (the
  2026-06-05 indexed-heap/decrease-key deferral remains the recorded
  candidate), or a per-consumer split keeping std heaps for floods —
  and only if a future profile shows the A*-side sift share grown
  enough that ~5% on the Deck justifies the added surface. The
  packed-node equivalence harness (differential oracle + goldens)
  carries over to any such attempt.

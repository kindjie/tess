## 2026-08-08 - Instruments for four unmeasured planner and collector paths

- Area: `collect_render_tile_deltas`, `resolve_local_coordination`, the
  `fields/*` product family, and the `include/tess/sim/` sentinel. No
  optimization in this entry beyond a one-line cleanup -- these are the
  measurements later changes will be judged against, per the standing rule
  that a performance change without before-and-after numbers from its
  owning family is not accepted.
- Gap: the 2026-08-07 audit found four places where a change would have
  moved no number. `collect_render_tile_deltas` runs a full chunk sweep per
  scheduler tick and had no benchmark at all -- the `render_delta/*` family
  exercises a different collector in `sim/delta_frame.h`.
  `resolve_local_coordination` inserts into a sorted vector, so R
  reservations cost O(R^2) element moves, against exactly one spatial
  benchmark at one fixed size. The whole `fields/*` family runs a 64x64
  world whose arrays are 16 KiB, measured at 0.001-0.007 LLC MPKI, so every
  product-layout change was measured only where memory traffic is free. And
  the sentinel representing `include/tess/sim/` measured thread creation.
- Instruments added (Apple M3 Max, `bench` preset, seven repetitions,
  `--benchmark_min_time=0.3s`, CV at or under 1.23%). Both readings below
  are the CONTROLLED ones: review found the first pair of each varying two
  things at once, and the corrected numbers differ materially from the
  first draft's:

| Benchmark | Median | Reads as |
| --- | ---: | --- |
| `render_delta/collect_scan_1_dirty_4096_chunks` | 1.227 us | floor |
| `render_delta/collect_scan_256_dirty_4096_chunks` | 2.034 us | 256x the dirty work, 1.66x the time |
| `spatial/local_coordination_1000x4` | 352 us | baseline |
| `spatial/local_coordination_4000x4` | 2.867 ms | 4x the requests, 8.2x the time |
| `fields/goalset_build_16` (64x64) | 80.4 us | cache-resident |
| `fields/goalset_build_16_512x512` | 155.6 ms | 64x the tiles, 1936x the time |

- Reading, render delta: 256 times the dirty work costs 1.66 times the
  time, so the fixed 4096-chunk sweep dominates and the 1-dirty cell is
  very nearly pure floor. That floor is the recorded M5 design-level
  backlog item; what was missing was any number attached to it.
- Reading, local coordination: 4x the reservations cost 8.2x the time.
  Linear predicts 4x and the quadratic insert alone predicts 16x, so the
  quadratic term is real and present but not yet dominant at these sizes. A
  single point could not have said that. Note the optimization log's
  existing retry condition for this area is keyed on OPTION counts, and the
  quadratic term is in the RESERVATION count, so the recorded trigger never
  covered it.
- Reading, field products: at a fixed 8x8 chunk extent, 64x the tiles cost
  1936x the time. That is far past the super-linear signature of leaving
  cache, and a single field build on a 512x512 world costs 155 ms -- an
  order of magnitude beyond a frame budget. The precedent for adding the
  cell at all is exact: the 256-chunk
  `storage/world_dirty_chunks_iteration` was flat at 125 ns both ways
  until the `_16k` variant existed.
- Recorded because it is a second measurement hiding inside the first, and
  because getting it wrong is what review caught: an earlier draft of this
  cell widened the chunk extent to 32x32 at the same time as the world, and
  measured 13.2 ms. Same tile count, 64x fewer chunks, **11.8x faster**. So
  chunk COUNT, not tile count, carries most of the growth here, and the
  original draft's ratio could not have been attributed to either. The
  shipped cell holds the extent at the family's 8x8 so the comparison
  varies world size alone. The chunk-extent effect deserves its own pair if
  it is ever worth optimizing.
- Also recorded, because it is the same class of error twice in one
  change: the render-delta pair originally marked its dirty set INSIDE the
  timed loop. The collector observes without consuming, so every iteration
  re-marked, and the pair timed 1 versus 256 `mark_dirty` calls alongside
  the scan it exists to characterize -- reporting 2.2x where the scan alone
  gives 1.66x. Setup is now hoisted out of the loop. This is the confound
  the `fields/cache_scan_entries_*` pair already had once, when goal counts
  varied while the entry claimed identical per-store work.
- Ceilings: bootstrap, at 4x these M3 readings, per the standing protocol.
  They were taken on an M3 Max while the gates run on Linux runners, so a
  tighter ceiling would flake rather than gate. A ceiling cannot see any of
  the ratios above; the paired cells are what make an exponent change
  legible.
- Sentinel correction: `include/tess/sim/` mapped to
  `parallel/tile_touch_scoped_threads_w4`, which measures thread CREATION --
  45x its serial control, and the widest interval of the twelve sentinels
  in the 2026-08-04 paired run. It now maps to the compute-bound
  `parallel/chunk_compute_pool_w4`. The thread-creation cell left the
  sentinel set entirely rather than staying unmapped, because
  `paired_bench.py` runs every DEFINED sentinel, not only mapped ones, so
  an unmapped one keeps costing a slot in every paired run while informing
  no directory. It keeps its threshold ceiling.
- Accepted, with no measurable effect: `grow_suffix_index` copied the whole
  slot table before reassigning it, where the old table is only read
  afterwards and `assign` reallocates regardless because capacity only
  grows. Now a move. Measured on the owning family: 14.752 ms -> 14.615 ms
  on `path/cached_astar_batch_100_mixed_repeated_room_portals_512x512`
  (-0.93%, inside the 1.11% before-side CV) and 17.204 us -> 17.212 us on
  `path/cached_astar_batch_100_suffix_open_512x512`. Recorded as no
  measurable delta and justified by inspection rather than dressed up: these
  caches hold 30 and 1 entries, so the growth path runs a handful of times
  on tiny tables. The family cannot resolve it and the entry says so.
- Deferred, with the reason, because the audit's premise does not hold:
  narrowing `PathScratch::parent_` from 8 bytes per node. The audit said the
  element type must follow `ShapeTraits<Shape>::TileKeyStorage`, but
  `KeyStorage<Bits>` is `std::uint64_t` for every width up to 64, so
  following it IS the current 8 bytes. Three things block a real narrowing:
  `parent_` stores global tile INDICES, which the sparse specialization
  deliberately leaves unbounded by residency, so a 32-bit element would be
  wrong there rather than merely tight; `PathScratch` is a non-templated
  public type, so the width cannot vary by shape without an API break; and
  storing dense offsets instead would need an offset-to-index inverse that
  does not exist and is not cheap for the sparse case. Revisit only
  alongside a deliberate `PathScratch` API change.
- Deferred: corridor chunk dedup is O(corridor^2)
  (`topology/topology.h`). The repository requires before-and-after numbers
  from the owning family, and the audit itself records this as negligible at
  the gated size with no longer route gated -- so those numbers are not
  producible today and nothing here adds a corridor instrument. Revisit if a
  long-route benchmark ever gates.
- Decision: Accepted.

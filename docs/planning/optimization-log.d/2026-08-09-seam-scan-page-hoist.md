## 2026-08-09 - Seam scans resolve two pages, not every tile

- Area: `detail::best_chunk_portal` (`path.h`), the seam scan behind every
  portal-route candidate. Follow-up to the 2026-08-09 on-device
  verification entry, which measured the function at 52.5% of the
  portal-first steady-state tick on the Steam Deck.
- Hypothesis: the scan called `is_passable` twice per seam tile pair, and
  each call ran `world.try_resolve` — a full coordinate resolution for
  tiles whose chunks are known. A goal-churn replan's candidate selection
  (six axis orders plus greedy, ~29 chunk steps each) performs ~13k such
  resolutions. Resolving the two pages once per seam and walking local
  tile ids should remove that arithmetic without touching semantics.
- Method: a fast path guarded by an in-grid check on both chunk
  coordinates and a checked page acquisition (`try_chunk` on sparse,
  `chunk()` on dense). Both pages present: three axis-specific loops call
  `Class::passable(page, local_tile_id)` with iteration order, scoring,
  tie-breaking, `scan_tiles`, and diagnostics accounting identical to the
  generic loop, which remains below as the authority for out-of-shape or
  non-resident chunks. Universal across movement classes — no span
  specialization (reviewer suggestion; adopted because hoisting the
  resolution is where the cost was).
- Evidence: accepted. Steam Deck (Zen2, performance governor, interleaved
  A/B/A/B, 3 repetitions per round, both rounds agreeing within 0.5%):
  `path/agent_tick_100_weighted_goal_churn_portal_512x512` 39.6 us to
  31.2 us (-21%), `path/weighted_chunk_portal_candidates_room_portals_512x512`
  21.3 us to 19.0 us (-11%), fresh-churn -2%, sealed-churn within noise
  (its cost is exact-fallback A*, not seam scans). M3 is flat on all four
  cells — the wide core hides the resolve arithmetic behind the seam-tile
  loads, so the win is device-specific and was only visible on target
  hardware.
- Equivalence: a differential test pins found/portal/scan_tiles against a
  per-tile-resolve oracle on a genuinely 3D asymmetric shape (4x8x2-tile
  chunks, 2x2x2 grid) across all 24 adjacent chunk pairs,
  all-pass/all-blocked/seeded-random patterns, exact single portals at
  both scan extremes, sparse missing chunks, and past-the-top
  out-of-shape neighbors (chunk coordinates are unsigned; stepping below
  zero wraps and fails adjacency instead). Three behavioral mutants
  verified caught (reversed loop direction, dropped source check,
  in-grid off-by-one). A loop-nesting-swap mutant survives and is proven
  equivalent: Manhattan tie sets on a seam plane are rectangles, and both
  ascending nestings visit the rectangle's minimum corner first. A
  diagnostics-enabled test asserts `path_passability_check` parity
  (one event per pair, a second when the source passes).
- Follow-ups: none scheduled. Cross-candidate seam memoization remains
  the recorded fallback if seam scanning is still dominant in the next
  on-device profile; the remaining portal-tick cost is now mostly cache
  stitching and movement.

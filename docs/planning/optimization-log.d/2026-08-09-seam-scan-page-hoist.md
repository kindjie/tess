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
  cells: the win is device-specific and was only visible on target
  hardware. A plausible mechanism — the wider core hiding the resolve
  arithmetic behind the seam-tile loads — is a hypothesis consistent
  with the flat A/B, not established by it.
- Equivalence: a differential test pins found/portal/scan_tiles against a
  per-tile-resolve oracle on a genuinely 3D asymmetric shape (4x8x2-tile
  chunks, 2x2x2 grid) across all 24 adjacent chunk pairs,
  all-pass/all-blocked/seeded-random patterns, exact single portals at
  both scan extremes, sparse missing chunks, and past-the-top
  out-of-shape neighbors (chunk coordinates are unsigned; stepping below
  zero wraps and fails adjacency instead). Six mutants verified caught
  fail-before: reversed loop direction, dropped source check, in-grid
  off-by-one, wrong forward source column, dropped second diagnostics
  event, and swapped loop nesting. The nesting swap initially survived
  the pattern suites and was wrongly recorded as provably equivalent
  (the rectangle argument only holds for unfiltered tie sets); a
  reviewer counterexample — two equal-score incomparable crossings —
  became a dedicated test that pins the authoritative nesting per axis.
  A diagnostics-enabled test asserts the fast path's
  `path_passability_check` short-circuit accounting (one event per
  pair, a second only when the source passes); it exercises the fast
  path only, matching the generic loop's emission rule by construction
  rather than by differential comparison.
- Follow-ups: none scheduled. Cross-candidate seam memoization remains
  the recorded fallback if seam scanning is still dominant in the next
  on-device profile; where the remaining portal-tick cost sits now is a
  question for that profile, not something these timings establish.

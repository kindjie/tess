## 2026-08-18 - Request-scoped memo for chunk-portal seam queries

- Area: `detail::select_chunk_portal_waypoints` and the two
  `best_chunk_portal` call sites beneath it. Accepted.
- Evidence for the target: a Steam Deck profile of
  `path/agent_tick_100_weighted_goal_churn_portal_512x512` put
  `best_chunk_portal` at 68.4-69.1% of sampled user-cycle self share
  across three captures, and an offline census of every call in two
  portal cells measured 67.13% and 66.68% of calls repeating a key
  already answered in the same tick. Cost-weighted rather than
  call-counted, those rates are 66.73% and 65.14% net of instrument, so
  duplicates are marginally cheaper than average but not materially.
- Shape of the duplication: every `(tick, seam)` pair carries exactly
  one distinct `(current, goal)` query, and each is visited by 3.04
  candidate routes with zero same-route repeats. The redundancy is
  therefore cross-candidate reuse of the same seam under the same
  query, never the same seam under a different query.
- Result, paired on device, main against branch, alternating rounds
  with bootstrap intervals: the repeated-goal portal cell falls 30,415
  to 19,045 ns (-37.3% [-37.5, -37.1]) and the fresh-goal portal cell
  134,296 to 125,585 ns (-6.4% [-7.2, -4.8]).
- Capacity: 128 and 256 entries measure the same within overlapping
  intervals (-36.8% and -37.3% on the repeated cell). 256 was chosen for
  headroom rather than speed: the measured maximum live entry count is
  74 per selection, and entry count scales with chunk distance, so the
  larger table saturates only on routes about three times longer. The
  packed entry is 32 bytes, so the thread-local table is 8 KB.
- Design: the key is `(tile index of current, signed six-way step)`. The
  goal is omitted because it is invariant across one selection; `from`
  is omitted because it is the chunk containing `current`, and a caller
  passing an inconsistent pair bypasses the memo rather than colliding.
  A generation stamp retires every entry when a selection begins, and an
  RAII scope makes a nested selection — reachable through a
  user-supplied passability predicate — bypass the memo instead of
  sharing its generation. Saturation falls back to the uncached call and
  is sticky, so a saturated selection does not re-walk the table on
  every later miss.
- Rejected along the way: a compile-time flag, because a macro that
  changes inline definitions in a header-only library is an ODR hazard
  and would leave the gated path untested; a `PathRuntimeCachePolicy`
  field, because the direct portal builders never receive one and the
  memo retains nothing across calls for a caller to reason about; and a
  hit-rate guard, because it would disable the memo during exactly the
  cold phase whose entries later candidates reuse.
- Mechanism confirmed on the merged binary, not inferred from the
  prototype. Scratch counters compiled in for one run reported 456,750
  memoized calls and 306,600 hits on the profiled cell - 67.13%, the
  census figure to the digit - and zero calls taking the non-keyable
  bypass across 46.6k calls in a second cell, so the
  `from == chunk_coord(current)` invariant holds everywhere reachable
  and nothing silently skips the memo. Independently, the public
  `portal_scan_tiles()` counter on
  `path/weighted_chunk_portal_product_room_portals_512x512` falls 7,456
  to 3,328 (-55.4%) between main and the branch; that cell's topology
  differs from the profiled cells, so its rate differs from 67% as
  expected. The scratch counters were removed before commit.
- Why the merged shape wins less than the prototype did (-37.3% against
  -43.2%): the hit rate is identical, so the difference is the
  consistency guard and the tile-index conversions the merged version
  adds, not fewer hits. The guard costs a `chunk_coord` per call and
  has never once rejected; it is kept because it converts a
  load-bearing assumption into an enforced property.
- The consistency guard was measured rather than argued about. Three
  shapes ran paired on device against each other: the guard as written,
  the guard moved onto the hit path with `from` stored in the entry and
  compared there, and the guard compiled out of release builds.
  Moving it to the hit path is 1.9% SLOWER on the repeated cell
  ([+1.7, +2.3]) - hits are two thirds of calls, so the extra compare
  lands on the majority path and the entry grows 32 to 40 bytes -
  and compiling it out of release measures -0.0% ([-0.3, +0.4]) and
  +0.1% ([-1.7, +0.9]), which is nothing. So the guard is free on the
  target hardware and stays in release: there is no cost to recover by
  weakening it. An earlier 2.3% figure from an unpaired host run was
  noise, as its overlapping variation suggested.
- Worth recording about the measurement: the exact-strategy control cell
  moves -1.2%, and the memo provably never runs there. That residue is a
  codegen and layout confound from the added code, so the portal figures
  contain an unquantified component of the same effect.
- Artifacts: `portal-tick-profile-2026-08-18`,
  `portal-redundancy-census-2026-08-18`,
  `portal-cost-weighted-2026-08-18`, `portal-cache-prototype`.

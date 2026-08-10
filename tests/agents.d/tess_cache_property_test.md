# tess_cache_property_test

- `tess_cache_property_test`: seeded sequences over `FieldProductCache`
  (redesign section 3.4, phase 7 slice b-ii). The caches carry the
  richest stated invariants in the library and had NO seeded coverage:
  every existing cache test drives a fixed hand-written sequence.
  Randomizes store, lookup, world edit (the only way staleness is
  reachable), clear, and an over-budget store across six keys against a
  three-product budget, checking after every step that a non-empty
  cache stays inside its byte budget, that entry and byte accounting
  match a from-scratch model, and that a rejected store changes
  nothing.

  The over-budget candidate is made oversized by carrying thousands of
  goals, NOT by lowering the budget. Lowering it evicts every resident
  first, so the store would be offered to an empty cache and the rule
  it claims to test — that a rejected store preserves existing entries
  — would be checked against nothing. A coverage gate requires
  refusals that happened while the cache still held entries.

  Two traps are load-bearing. `hits + misses == lookups` is WRONG: a
  stale match erases its entry and counts as a rejection, not a miss,
  so the identity is `hits + misses + stale_rejections`. And residency
  is not validity — a world edit invalidates every product while the
  entries stay resident until a lookup discovers it, so a probe after
  an edit finds nothing though the cache still reports entries.
  Conflating those two is what the LRU test caught on its first run.

  **`TheModelsResidencyPredictionMatchesTheCache` does not verify the
  eviction policy, and its name says so on purpose.** Mutation testing
  showed a model predicting FIFO instead of LRU passes it unchanged:
  the sweep yields only a handful of hits per seed, so the state where
  the policies disagree is essentially never reached, and neither
  reweighting the actions nor shrinking the key space changed that.
  `ALookupRefreshesRecencyAndSavesAnEntry` constructs that state
  directly — fill to budget, refresh the oldest entry, force one
  eviction — and is the test that owns the LRU claim; inverting its
  expectations makes it fail, which is what establishes it can.
  A second model covers `RouteCacheScratch`, whose policy is different
  enough that a shared model would assert something false: it has NO
  eviction, so an insert breaching either the entry or node cap
  invalidates the WHOLE cache, and a single route longer than the node
  cap is skipped outright instead. Asserts both caps as hard bounds,
  that every query resolves as exactly one of exact hit, suffix hit or
  miss, that a served route reports zero expanded and reached nodes
  (the observable signature of not having searched), and that an
  oversized skip leaves residents alone.

  That last one needs a carve-out the harness found on its own:
  `cached_astar_path` binds the movement class at the START of the
  call, and a rebind drops every entry by design, so a call that both
  rebound and skipped legitimately empties the cache. Attributing that
  loss to the skip was a modelling error, caught and shrunk to the
  four-operation sequence `20,16,21,18` with a replay command that
  reproduced it.

  Note the node cap is 40, not 64: at 64 the corner-to-corner route
  (63 nodes on this grid) fit inside the cap, so nothing was ever
  skipped. The coverage gate caught that too.

  A third model covers `WeightedPortalSegmentCache`, the third distinct
  policy in three headers: an ENTRY budget rather than a byte budget,
  sweep-then-evict-oldest rather than LRU, and stale entries that
  linger until a sweep reclaims them. Asserts the budget bound, that a
  miss or stale entry leaves the caller's output buffer untouched, and
  that re-storing a request does not add a second entry — but only
  while the existing entry is still LIVE, because `find` skips a stale
  match without erasing it, so below budget a re-store legitimately
  appends a duplicate. The unconditional idempotence claim is false.

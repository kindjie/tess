# tess_pibt_movement_test

- `tess_pibt_movement_test`: PIBT movement tier coverage — the width-3 ring
  gate cell (64x64, n=48, pinned seed) through the real weighted tick driver
  with the full settled recipe, where the joint commit's congestion lets
  arrivals seal one agent's goal off (verified by a class-consistent BFS)
  while PIBT keeps everyone moving and solves; a deterministic dead-end
  head-on where resolution requires stepping off the route into a side
  pocket, wedged forever by the joint commit under `Forbid` and resolved by
  priority inheritance without swaps; a terrain-only ranking oracle under a
  settled-aware class parks an agent beside an obstruction forever while a
  class-consistent oracle detours; priority inheritance displaces a
  lower-priority blocker in the same tick; backtracking falls back to the
  chooser's next candidate when the inherited peer is boxed; a failed peer's
  tile stays claimed so later deciders cannot stack onto it; tiles occupied
  by anything outside the agent span are never entered; an impassable source
  fails `BlockedFrom` exactly as `commit_movement_intent`; `max_steps` zero
  pauses movement and larger values multi-step with early exit; the swap
  counter counts only secured exchanges (a policy-allowed reverse edge that
  loses its vertex claim counts nothing); head-on pairs
  follow `SwapPolicy` with swap/denial counters; outcomes are deterministic
  across identical runs; the warm pass allocates nothing after
  `PibtPriorities::reserve` plus `JointMoveScratch::reserve`; `elapsed` is
  reachable while `order` and `frames` are not, and mixing that public
  member with a private one costs standard layout; and
  `DistanceFieldProduct::distance_at` returns exact distances in-shape and
  the `unreachable_distance` sentinel for unreached tiles, out-of-shape
  coordinates, and shape-identity mismatches;

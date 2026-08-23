# tess_reciprocal_conflict_test

- `tess_reciprocal_conflict_test`: PR C3's reciprocal-conflict screen
  (issue #247) as a permanent regression suite. Five hand-built fixtures
  (pocket-yield corridor, Permit head-on, 2x2 rotation under both swap
  policies, junction cross, queued yields), an exhaustive joint-space BFS
  oracle for exact optimal makespans, and four arms: production PIBT
  (decision), joint movement, sequential movement, and PIBT under exact
  BFS ranking (all context).
- Verdicts are PINNED AT THEIR OBSERVED VALUES, and three production-tier
  pins are FAILURES (pocket_yield, junction_cross, queued_yields wedge or
  self-seal under Forbid). A red run on those tests can therefore mean the
  tier IMPROVED -- that is PR C4's flip signal, not a regression. Flip a
  pin only with the movement-stream record that explains the change.
- The oracle has hand-computed anchor cases because a wrong conflict
  model would pass every fixture silently: the 2x2 rotation is makespan 1,
  the Permit head-on is 5 (parity forces one deviation tick, not 4), and
  the Forbid bare corridor is unsolvable.
- Fixture digests mix the declared SwapPolicy into `scenario_digest`,
  because the two rotation fixtures differ only by policy and the base
  digest cannot see it.
- The failure mechanism worth remembering: a mid-corridor pocket is
  enterable only from one tile, and a myopic yielder pushed backward past
  that tile can never recover it, under EITHER ranking. The pinned
  regression's pocket-yield (tess_pibt_movement_test.cc) passes because
  its yielder is cornered adjacent to the pocket; these fixtures require
  one step of foresight.

# tess_property_test

- `tess_property_test`: the seeded property/state-machine harness
  (`tests/property_harness.h`, redesign section 3.4). Drives random
  operation sequences against a model, checks every invariant after
  every step, shrinks a failure by delta debugging, and prints a
  replay command. Covers residency (ensure/touch/evict/mark-dirty
  against resident-count and byte-budget ceilings, the
  resident-implies-nonzero-generation pairing, and generation
  stability across a redundant ensure) and schedule ticks
  (`tasks_run + tasks_skipped == tasks_due`, monotone tick) — both
  areas that previously had NO seeded coverage, only fixed
  hand-written sequences. It also asserts that a newly materialized page
  never reuses a generation, which is what makes an evicted chunk's
  stale handle detectable.

  Three things about this harness are load-bearing and must not be
  weakened. A deliberately broken model proves the harness can fail: it
  must find a defect that needs two specific operations, shrink 64
  steps to exactly those two, and produce a sequence that reproduces.
  A coverage test asserts the residency sweep actually fills the world
  and evicts — an earlier operation encoding could only address three
  of six chunks, so a four-chunk world never filled and every ceiling
  invariant was asserted against a world incapable of violating them.
  And an unusable `TESS_PROPERTY_REPLAY` fails loudly instead of
  falling back to the sweep, because a blank value once parsed as an
  empty sequence and skipped the sweep entirely while reporting a pass.

  Shrinking is 1-minimal, not globally minimum: only chunk-aligned runs
  are dropped, and a candidate is kept only if it reproduces the SAME
  invariant, so a shrink cannot drift onto a different, easier
  violation. Bounded to 24 seeds x 64 steps on the pull-request tier;
  the weekly `Long-Seed Property Sweeps` job raises both through
  `TESS_PROPERTY_SEEDS` and `TESS_PROPERTY_STEPS` (400 x 192; the other
  property suites choose their own step defaults). A malformed or zero
  value is an ERROR, not a silent
  fallback — a weekly run that quietly executed the pull-request
  workload would report a long-seed pass it never performed. The job
  proves the override took effect by checking that a zero budget is
  rejected, rather than trusting that exporting a variable did
  something.

  Tests that read the budget must UNSET the variables when asserting
  defaults: the weekly job exports them, so assuming a clean
  environment fails there. Running the real weekly workload locally is
  what surfaced that.
  The printed replay command names no build directory on purpose — a
  failure found under ASan does not reproduce against a `build/dev`
  binary — so run it from the build directory that produced it.

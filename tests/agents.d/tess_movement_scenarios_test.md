# tess_movement_scenarios_test

- `tess_movement_scenarios_test`: self-test for the shared movement fixture
  and classification harness in `movement_scenarios.h`, which decides what
  the post-0.13 movement experiments (C1, C3, X1) can compare. A harness that
  arbitrates three experiments is tested before it is trusted.
- Seeds are a closed formula `f(family, trial)`, not a curated list, and the
  trial counts are pre-registered in the header. A list would let a seed be
  dropped after its result was seen; the formula leaves no per-seed freedom.
  The collision test exists because two families sharing a seed would make
  one instance count as two independent trials.
- Instance digests are committed because a single test binary cannot compare
  itself across build configurations. A changed digest means every previously
  recorded result for that family refers to a different instance, so
  regenerate deliberately and never to clear a red test.
- The classifier tests are hand-built rather than generated: sealed versus
  wedged is the distinction the whole harness exists for, and a generated
  instance cannot prove which category it should land in. Misreading that
  distinction produced a wrong conclusion once already (the PIBT tier's
  original gate evidence).
- Termination is a no-progress fixpoint with the tick cap demoted to a safety
  bound. Classifying at a cap would be invalid: the terminal set grows
  monotonically, so it is final only at quiescence, and under a shared cap
  slower arms accumulate false residuals. Capped runs report `Censored` and
  are excluded from every metric.
- `WedgeDetectionRequiresSustainedNoProgress` guards the stopping rule's
  false-positive mode, a run that pauses briefly and then resolves.
- The ring family is pinned at zero wedges. An earlier revision settled agents
  without invalidating retained routes, so a stale-routed agent parked instead
  of replanning and the harness manufactured 354 wedges out of 960 on a
  lattice the tier solves completely. That artifact was larger than the effects
  the downstream arms are hunting and would not have cancelled across them.
- Two consecutive runs of one seed must agree. That is the test that catches
  `PibtPriorities` state leaking between runs: it is index-paired with the
  agent span and only grows, so a reused instance carries stale `elapsed`
  into the next run and silently changes decision order.
- The anonymous-goal-pool mode (C2) is arm-neutral by construction: agents
  are placed goal-less, so assignment is arm code and supersession
  accounting starts at zero. The pool consumes the same shuffled goal
  stream as the paired mode, which two tests pin: at M = N the pool IS
  the paired instance's goal sequence, and larger pools extend smaller
  ones as prefixes with terrain and starts unchanged. Its digest table
  is separate from the paired table because the pre-registration forbids
  fixture changes after arm code exists; the placement code deliberately
  mirrors `build_scenario` rather than sharing a helper so one refactor
  cannot shift both tables in one edit.

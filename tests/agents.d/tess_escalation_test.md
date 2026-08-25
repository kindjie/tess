# tess_escalation_test

- `tess_escalation_test`: PR C4 Phase A (issue #253 + amendments) --
  conflict-local temporal escalation as harness-level test support in
  `escalation_harness.h`, gated against the shared reciprocal fixtures.
  No library code changed; the C3 plain-tier pins still describe the
  production tier, and this suite pins what escalation adds on top.
- All three C3-failing conflicts resolve within C3's own bound:
  pocket_yield (region growth to radius 4 recovers the mid-corridor
  pocket), junction_cross, and queued_yields -- the last via the
  imminent-seal trigger, because a seal can only be PREVENTED, never
  repaired, under terminal-set monotonicity.
- The substrate test pins a MEASURED PARTIAL FAILURE, not a success: the
  per-agent non-worsening gate failed on 2 of 61 residual seeds
  (trajectory divergence can strand an agent far from any fired
  component), so always-on arming was not accepted and Phase B is not
  proposed. Clean seeds are strictly inert (zero fires,
  digest-identical, all 71). Flip these pins only with a successor
  mechanism's own evidence.
- Mechanism hazards pinned by the fixed defects: settled agents inside a
  region must not invalidate a plan they were modeled in
  (planning-time position snapshot); an aborted plan tick must fall
  through to a tier tick or fire/abort cycles starve the population
  into the wedge rule; both triggers need cooldowns.
- The per-seed substrate tests pin OUTCOME DIGESTS (plain and armed)
  as cross-build constants for a deterministic subset -- trials 0-1 of
  each family plus every divergent seed. One armed settle per seed,
  compared against the pinned constant, proves determinism more
  strongly than an in-process replay at a third of the cost; that is
  what keeps the fire-heavy seeds inside the 60-second contract on
  sanitizer and MSVC-debug runners, where capped escalation solves run
  an order of magnitude slower. Consequences stated plainly: the plain
  runs are not re-executed by the merged suite (their digests are the
  pins), trials >= 2 of non-divergent seeds are covered by the evidence
  sweep ONLY, and the aggregate (+3/-2/-1) and the 71/61 clean/residual
  split are asserted nowhere in the merged suite -- the recorded sweep
  program is their check, and since the pre-RC audit that program
  ASSERTS the recorded aggregates and replays both arms with digest
  comparison, exiting nonzero on drift. The full 132-seed sweep is
  evidence, captured with its program in
  `docs/planning/evidence/v1.0/c4-escalation/`. Solver cost
  needed three layers (packed nodes, a futility memo, and the
  amendment-2 cap verified outcome-identical) before the armed sweep
  was affordable at all -- the recorded promotion blockers include that
  story.

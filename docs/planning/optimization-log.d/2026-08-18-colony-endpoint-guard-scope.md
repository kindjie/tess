## 2026-08-18 - Endpoint guard narrowed to substantial barriers

- Premise: the optional browser policy should spread routes around interior
  congestion while retaining the known safety fallback for a dense one-sided
  barrier immediately before an endpoint band.
- Reproduction: replay the checked-in `browser-guard` native scenario, whose
  297 wall coordinates preserve the user-drawn browser fixture, with 1,024
  agents in canonical and spread modes. Seven wall tiles crossed the protected
  approach zones: five along one horizontal wall on the left and two along
  another on the right.
- Finding: the original any-tile guard silently disabled the option for the
  entire leg. Canonical and checked spread modes were identical at 3,277 ticks,
  388,436 routed waits, 1,458 low-progress ticks, and zero seed waves. Both
  still reached all 1,024 goals, so terminal outcome alone hid the policy
  suppression.
- Controlled probe: retain every wall but bypass only the global endpoint
  veto. One normal seed wave then completed all 1,024 agents in 455 ticks with
  25,166 waits, no crowd-blocked or unreachable agents, and no low-progress
  ticks. The existing one-shot merge detector observed 22 merge tiles and
  correctly scheduled no second wave.
- Change: count accepted construction tiles in each eight-column approach zone
  and suppress spreading only when either zone contains at least 64 tiles,
  half the map height. This keeps the mechanism demo-local and additive; it
  does not infer portals, change passability, or alter movement authority.
- Verification: the central two-gate native fixture now includes sparse wall
  touches in both approach zones and must still schedule exactly one seed wave.
  Direct 63/64 boundary checks include duplicate submissions. The existing
  96-tile goal-wall control remains canonical with zero seed waves, and
  maximum-scale terminal checks remain authoritative.
- Decision and limit: accept the narrower guard. The 64-tile threshold
  distinguishes the two measured geometries without claiming a general
  endpoint-capacity proof. Reconsider it only with a failing deterministic
  endpoint fixture; compare terminal outcomes before optimizing tick or wait
  counts.

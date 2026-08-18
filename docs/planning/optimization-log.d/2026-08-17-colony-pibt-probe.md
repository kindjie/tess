## 2026-08-17 - Browser colony PIBT probe rejected; progress metrics added

- Area: the 128x128 browser colony at 1,024 agents, especially the bounded
  planning ramp and the natural goal-wall fixture used by the native
  self-check.
- Hypothesis: an optional PIBT pass with route-attachment ranking would let
  routed agents yield locally, reduce convoy waits, and remain within the
  demo's eight-query replan budget.
- Baseline profile: an Apple M3 Max, AppleClang 21, `-O3`, `-g`,
  `-fno-omit-frame-pointer`, `-mcpu=native` build ran the complete native
  self-check in 1.288 s mean (0.027 s standard deviation, ten runs). A
  2,000 Hz Samply capture collected 2,717 samples; 40.78% of leaf samples
  landed in the joint-movement advance. The aggregate self-check mixes
  scenarios, so it identifies a candidate hot path rather than a causal
  result for the reported screenshot.
- Microbenchmark context: on the existing 128x128, 1,024-agent lab cells,
  the median joint versus PIBT chain-reset pass was 0.890 versus 0.246 ms.
  Fully denied contention was nearly equal at 0.154 versus 0.164 ms. These
  cells rank movement-pass cost but do not model the demo lifecycle.
- Paired probe: one fixed tick per iteration recorded awaiting-plan
  agent-ticks, valid-route waits, off-route moves, queue depth, completion,
  and elapsed time. Joint movement completed the open leg in 243 ticks and
  quiesced the goal-wall leg in 537 ticks at 374 arrived plus 650
  crowd-blocked. A naive PIBT composition incorrectly moved routeless agents
  through its Manhattan fallback, bypassing the bounded planner; it completed
  the open leg in 116 ticks but failed to quiesce the wall fixture after
  5,000 ticks (401 arrived, no classified crowd-blocked agents, 26.6 s).
- Constrained probe: forcing agents without a valid retained route to stay
  restored the open result to the same 243 ticks, but raised elapsed work from
  about 49 to 121 ms. The wall fixture still failed to quiesce after 5,000
  ticks (550 arrived, no classified crowd-blocked agents, 18.0 s): reactive
  yields kept agents live instead of allowing the settled-only recovery
  classifier to finish the leg.
- Decision: rejected for the browser demo. Do not expose the PIBT toggle or
  weaken the bounded planning FIFO. Add O(1) page diagnostics for pending
  plans, last-tick advances, and last-tick movement waits so future captures
  distinguish planning backlog from routed contention. Joint movement and
  all core library semantics remain unchanged.
- Limitations and retry condition: timings are local and uncontrolled, and
  the paired fixture reproduces the maintained goal-wall case rather than the
  exact user-drawn wall coordinates. Revisit PIBT only if the new diagnostics
  show sustained partial occupied waits after the plan queue drains and a
  lifecycle design lets local yielding coexist with bounded replanning and
  settled-only terminal classification. Evaluate caller-owned lane or
  waypoint assignment separately because PIBT does not change the
  individual-shortest-path objective that creates aligned convoys.

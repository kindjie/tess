## 2026-08-16 - Tiered and 1024-capacity mixed campaigns on controlled hardware

- **Area:** budgeted-progress mixed-colony benchmark; movement tiers
  (baseline vs PIBT with the route-attachment ranking); capacity ladder.
- **Setup:** Steam Deck LCD (Zen 2), process pinned to an isolated
  physical core, performance governor, sleep inhibited for the full
  window; serial executor; seed the canonical colony seed; ten
  repetitions per cell, all seven frame budgets, fidelity view. Two runs:
  the 512 tiered matrix (both tiers × 20/60 TPS × populations
  100/250/500, 84 cells, at the movement-tier commit) and the 1024
  capacity ladder (both tiers × 20 TPS × populations
  100/250/500/1000/2000, 70 cells, at the 1024-cell commit). Device
  temperature spot checks: 42-51 °C across launch/end boundaries; no
  continuous thermal trace. All 154 artifacts pass the fail-closed
  validator with no matrix holes; artifacts stamp
  `machine_fingerprint: local-uncontrolled`, and the campaign manifest
  kept with the archived bundle is the hardware-provenance record.
- **Results (512 world):** the PIBT tier flips `flow_stable` to true at
  all four p100/p250 rungs on device (deadline success 0.994-0.999,
  zero starved, oldest outstanding age 24-28 ticks versus baseline's
  window-bound 240/720); p500 is unstable on both tiers at both rates,
  so the closed-loop capacity boundary sits between 250 and 500 agents
  on this map at these densities. PIBT useful throughput reaches
  2.3-3.1× baseline at p500. Baseline cells reproduce the earlier
  single-tier campaign byte-for-byte across two commits, so the tier and
  world-shape refactors left baseline dynamics untouched.
- **Results (1024 world):** baseline stabilizes at no rung (success
  0.86-0.88 with starvation scaling to 7 500 items at p2000). PIBT holds
  success 0.990-0.995 with zero starvation through p2000 at ~2.4-2.5×
  baseline throughput. Per-agent throughput is flat across the entire
  ladder for both tiers (baseline ~34, PIBT ~82-83 useful per agent):
  at one quarter of the 512 world's agent density the map is
  contention-light and scaling stays linear through 2000, so PIBT's
  margin here is wedge resolution and ranking quality rather than
  congestion relief.
- **Stability-flag inversion, quantified:** under PIBT at 1024 the
  aggregate miss rate is 1.03 % at p100/p250 (per-rep 0.99 gate barely
  missed → flag false) versus 0.52-0.72 % at p500-p2000 (flag true).
  Every miss is a late completion bounded at 25 ticks of lateness
  (lateness p99 is 0-2 over full-cohort samples); none starve. The
  working hypothesis is structurally long wall detours — scale 16
  doubles detour lengths against the fixed 32-tick allowance — but the
  causal association (miss ↔ admission-time static route distance) is
  not yet instrumented, so this stays a hypothesis; the artifacts lack
  per-repetition sidecars to settle it.
- **Flow stability is not frame safety:** at 1024/p2000 the mandatory
  tick body reaches ~75 ms (baseline) / ~96 ms (PIBT) p99 against the
  50 ms frame period, with frame-start lag p99 of ~59 ms / ~79 ms. A
  rung can be flow-stable while overrunning every frame; the two
  properties must be read from different columns.
- **Realized churn:** applied-edit hashes diverge across tiers at
  512/p500 (both rates) and 512/p250/60 TPS, and agree at 512/p100,
  512/p250/20 TPS, and every 1024 rung. Agreement proves an identical
  applied-edit sequence, nothing more.
- **Limitations:** peak-RSS values pool across process order (RSS
  high-water never shrinks), so no per-cell memory claims are made; no
  continuous thermal/frequency trace; the 60 TPS axis was not run at
  1024 (population is that cell's axis; deferred unless needed).
- **Decision:** `flow_stable` stays advisory — no gate. Reconsider a
  PIBT-tier-scoped gate only after allowance semantics are calibrated:
  any scaled allowance must derive from an immutable admission-time
  static shortest-path distance, never realized route length (which
  would reward congestion and pathological rerouting).
- **Follow-up:** instrument the miss ↔ static-route-distance
  association to settle the detour hypothesis; calibrate allowance
  semantics; consider per-repetition cohort sidecars in the artifact
  schema; the 1024/60 TPS axis if a consumer needs it.

## 2026-08-15 - Budgeted-progress stage-4 campaign on controlled hardware

- **Area:** budgeted-progress benchmark suite (frame-budget controller,
  isolated cells, capacity search, counter pass, mixed colony).
- **Setup:** Steam Deck LCD (Zen 2, 4c/8t), SteamOS, process pinned to an
  isolated physical core, performance governor, steamrt4 container build,
  serial executor. Timing pass and counter pass at one commit; the mixed
  matrix at the seating-fix commit that followed (placement stride 2), with
  the two cohorts labeled and never pooled. Artifacts validate fail-closed
  and curves regenerate from artifacts alone; artifacts stamp
  `machine_fingerprint: local-uncontrolled`, so the campaign manifest kept
  with the archived artifacts is the hardware-provenance record. Device
  temperature 44-53 °C across pass boundaries with rotated budget order;
  no continuous thermal trace was captured, so drift is mitigated and spot
  checked rather than conclusively excluded.
- **Results (timing pass, isolated):** the A* unit cell is clearly
  sub-linear in budget: each 4× budget step (0.125 → 0.5 → 2 → 8 ms) yields
  1.41× / 2.21× / 3.12× useful completions/frame (1.56 → 15.1 end to end),
  with work per completion in a 4.3 % band (7 527-7 847 units) and identical
  trace hashes. Cell shapes differ by design: queued and resumable cells
  scale near-linearly with budget while the field-product cell is flat.
  Overshoot stays at small per-frame rates with the quantum-tail bucket
  dominant.
- **Results (capacity):** paced-arrival capacity bands confirm stable rates
  69 / 83 / 184 / 780 items/s at 0.125 / 0.5 / 2 / 8 ms — monotone,
  non-inverting, 1.2-1.9 % wide, zero flapping. The 0.5 ms confirmation is
  borderline: 3 of 5 confirmation repetitions passed and pooled deadline
  success is 98.96 %, just under the 99 % target, accepted by the documented
  majority policy (the table's rounded 0.990 hides this).
- **Results (counter pass):** all 20 timing/counter pairs passed the
  designed trace-identity, conservation, and applicable comparison gates.
  By design several statistical comparisons do not apply (saturated A*
  cells complete no full pool wrap per repetition; overloaded arrival pairs
  and saturated throughput carry no tolerance), and counter-build resumable
  throughput runs 31-37 % above timing — instrumentation cost shifts wall
  figures, which is why counter wall numbers are never published.
- **Results (mixed colony, populations 100/250/500 at 20/60 TPS):** within
  every (TPS, population) point the flow counters, class records, useful
  work, deadline success, and age metrics are exactly identical across all
  seven budgets — structural, since every mixed task is tick-coupled
  mandatory work with no defer-capable quantum; the budget changes only the
  overshoot classification. Frame-start lag is likewise population-driven,
  not budget-driven (~90 ns at p100; 28.35-28.40 ms at p500/20 TPS across
  all budgets, the tick body outrunning the 50 ms frame period). Population
  scaling is monotone but sub-linear: 1 : 2.5 : 5 population gives
  1 : 2.18 : 3.59 useful/frame at 20 TPS (per-capita throughput falls ~28 %
  from p100 to p500).
- **Finding:** `flow_stable` is false at every mixed point, and on two
  criteria at once: interactive-class deadline success runs 0.893-0.979
  against the 0.99 gate, and final oldest outstanding age reaches 240 ticks
  (20 TPS) / 720 ticks (60 TPS) against the 128-tick threshold. Misses are
  starvation-dominated — items that never complete through settlement
  (310 → 1 610 of ~4.4 k → 15.9 k admitted as population grows at 20 TPS) —
  rather than a lateness tail. This is a property of closed-loop ping-pong
  demand with churn at stride-2 placement density, not a budget effect; the
  stride-2 interference explanation is plausible but untested (no controlled
  cross-stride comparison exists, and the earlier stride-8 cells were
  superseded by the seating fix).
- **Artifact bug found in review:** the mixed cell records only positive
  lateness samples while labeling the family `completed_cohort_items`, so
  published lateness percentiles describe only the 0-140 late completions,
  not the full cohort. Deadline-success rates are unaffected. Lateness
  percentiles from this campaign are excluded from conclusions until the
  sample base is fixed or relabeled.
- **Incident:** the first timing pass aborted silently partway into the
  mixed matrix — stride-8 placement seats at most 227 agents, and the abort
  message was lost to stdout block buffering in the redirected log. Fixed
  (stride parameterization, up-front seating preflight, counted error
  message, line-buffered stdout, `--mixed-only` resume flag) with a
  bench-path seating test at the ladder maximum.
- **Decision:** accept the campaign as stage-4 acceptance evidence for the
  suite; record the mixed-stability shortfall as a workload finding, not a
  regression (no gate exists at these matrix points). The 512² /
  {100, 250, 500} ladder is a campaign subset of the design's canonical
  1024² / 1000-agent ladder.
- **Follow-up:** fix the mixed lateness sample base (record zero-lateness
  completions or relabel the family); starvation attribution in the mixed
  cell (churn-unreachability versus contention) before any stability gate
  is proposed; the canonical 1024² / 1000-agent rung needs the larger world
  and a fresh seating-capacity check.

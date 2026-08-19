## 2026-08-17 - Multigate topology guard prevents harmful spreading

- Hypothesis and trigger: a repository-owned scenario runner would expose
  policy regressions hidden by the hand-maintained experiment notes. The
  optional seeded policy must improve congested convergence without replacing
  natural route diversity already supplied by a broad interior barrier.
- Reproduction surface: `tess_web_colony_model --scenario <geometry>
  --agents <count> --mode <canonical|spread> --max-ticks 5000` prints fixed
  ticks, waits, outcomes, queue and query ceilings, seed-wave count, one-agent
  progress counts, and uncontrolled elapsed time. `--require-complete` makes
  an incomplete or adverse terminal outcome fail the command.
- Finding: the current 1,024-agent four-gate fixture completed canonically in
  600 ticks with 29,262 waits, but unguarded spreading took 945 ticks with
  40,621 waits. The result reproduced through both the new runner and the
  earlier temporary harness. The older improvement claim was stale after the
  protected endpoint layout changed.
- Controlled change: only after the existing option, endpoint, population,
  and wait gates pass, inspect the fullest interior construction column
  straddled by at least one quarter of the active start-to-goal traffic. If it
  spans at least half the map and exposes more than two contiguous open runs,
  mark the one-shot observation complete without scheduling a seed. One- and
  two-opening barriers retain the existing policy. Disabled and uncongested
  ticks perform no topology scan; a native regression proves an equally dense
  accumulated wall behind the cohort cannot suppress the active barrier.
- Compact campaign: spreading completed every tested case with at most eight
  exact planning queries per tick. At 1,024 agents, wall tip remained
  1,305 ticks / 309,363 waits and two gates remained 867 / 65,187, while four
  gates became the canonical 600 / 29,262. At 640 agents the corresponding
  results were 826 / 99,719, 653 / 31,823, and canonical 423 / 9,780. At 128
  agents they were 398 / 1,488, 185 / 199, and canonical 210 / 366. Open and
  guarded goal-wall controls remained unchanged at maximum population.
- Verification authority: native CTest smoke coverage proves the CLI's
  terminal outcome and a 512-agent four-gate run proves zero seed waves. The
  existing native checks continue to pin the two-gate activation, wall-tip
  second wave, topology cancellation, and eight-query ceiling.
- Decision and limit: accept the demo-local topology guard. It represents the
  material distinction demonstrated by the fixture without introducing a
  general portal abstraction or capacity model. Reconsider only if a barrier
  with more than two openings demonstrably benefits from seeding on a broader
  deterministic corpus; terminal outcomes and comparative tick/wait counts
  remain authoritative over the fixed opening threshold.

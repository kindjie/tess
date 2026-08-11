## 2026-08-10 - Per-test fragments record purpose and traps

- Decided: each `tests/agents.d/` fragment carries a compact statement of what
  its test pins plus facts that cannot be recovered safely from case names and
  implementation alone: chosen constants and their rationale, deliberately
  narrow claims, untestable exclusions, mutation findings, assertion
  ownership, and load-bearing coverage gates.
- Behavior inventories belong in test sources, where case names, fixtures, and
  assertions keep them reviewable with the implementation. Repeating those
  inventories in prose creates an unverified drift surface; no gate can prove
  that a comma-separated catalogue still matches the test.
- When a source comment already records a trap, the fragment references that
  comment instead of maintaining a second copy. This is allowed only after
  confirming that the comment carries the rationale, not merely the result.
- Editorial removals remain conservative. Specific rationale survives even
  when verbose, because a lost trap fails silently while an extra sentence is
  cheap. The first-line and one-fragment-per-test mirror remain unchanged and
  CI-enforced.

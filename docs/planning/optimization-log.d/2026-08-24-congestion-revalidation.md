## 2026-08-24 - Congestion pricing revalidated at full supported coverage; retained as a caller recipe

**Question** (pre-registered in issue #256; amendment 2 added the
plan-mandated demo-classifier judge after review caught the original
registration gating on the wrong harness; amendment 3 -- posted before
its run -- added the full supported-population matrix, exact gates,
and a pre-declared value rule after a second review round showed the
two-population sample could not close the plan's "every supported
population" gate): does one bounded dynamic price policy (per-tile
cost `1 + min(3, live agents within Manhattan 1)`, every 4 ticks,
versioned edits) preserve terminal classification and buy anything?

**Answer: retained.** Across seven scenario geometries (the native
CLI's full set, browser-incremental's progressive wall admission
included per the amendment-3 addendum) x all 64 supported populations
(448 cells): priced classification retained or
improved everywhere, zero crowd-blocked and zero durably-unreachable
in the priced arm, replay bit-identical, every scenario wall admission
assertion-checked, and the canonical tier's own 41 arrival-incomplete
tip cells (every population >= 384, stranded at the 5000-tick cap)
all complete under pricing. Pre-declared value rule: pooled gm of
priced/canonical ticks 0.4180, CI [0.3859, 0.4522] -- PASS, on both
platforms with byte-identical tables. The
boundary in exact numbers: goal-wall regresses at gm 1.49 (up to
+89%) with classification intact, and the originally-registered C0
substrate screen stays failed as the sensitivity record (17 of 132
fixpoint seeds reclassify chaotically). Outcome-level only: waits and
gate utilization were not instrumented, so no mechanism claim is made.

**Consequences.** Pricing is documented as a caller recipe with its
boundary in the spatial-coordination architecture notes; no library
change, demo spread default unchanged. C6 dispositions without a run:
no MECHANISM-level capacity premise was isolated for a crossing
reservation to represent (the earlier "served premise" phrasing
overclaimed and is corrected). The review sequence itself is part of
the record: two rounds each invalidated a verdict-carrying element
(wrong judge; insufficient coverage), and both corrections were
registered before their reruns.

Evidence: `docs/planning/evidence/v1.0/c5-congestion/`.

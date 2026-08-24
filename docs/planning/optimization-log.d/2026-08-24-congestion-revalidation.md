## 2026-08-24 - Congestion pricing revalidated and re-rejected on the pinned substrate

**Question** (pre-registered in issue #256): does one bounded dynamic
price policy -- per-tile cost `1 + min(3, live agents within Manhattan
1)`, recomputed every 4 ticks, written through the versioned edit
channel -- preserve terminal classification on the C0 substrate and buy
anything, or does the colony-era rejection reconfirm on the corrected
baseline?

**Answer: re-rejection, with the original signature.** The parity gate
fails on 12 of 132 seeds; the priced arm loses arrivals canonical keeps
(warehouse t3/t17, colony t1/t2/t4/t11 -- the historical
incomplete-arrival mode, now on pinned seeds), while other marginal
seeds improve (colony t0/t3/t13/t15, warehouse t4): a local price
signal perturbs marginal instances chaotically in both directions, the
same trajectory-divergence mechanism C4's substrate measurement
documented. Aggregate failures barely move (330 -> 328 colony, 10 -> 11
warehouse) and the tick metric is flat where classification agrees
(pooled gm 0.9977, CI [0.9810, 1.0180] -- includes 1.0). Determinism
held everywhere, and the scripted edit replay with pricing active
composed correctly with topology invalidation (deterministic,
uncensored, all arrived) -- the machinery is sound; the policy simply
has no systematic value and real parity costs.

**Consequences.** No library change was ever involved (the substrate's
cost field and versioned edits express the whole policy), so nothing
merges or unmerges; the record is the disposition. C6's opening
condition is answered: no capacity-contention premise was isolated --
divergences are chaotic, not hotspot-shaped -- so the capacity-aware
crossing reservation dispositions without a run in X3.

Evidence: `docs/planning/evidence/v1.0/c5-congestion/`.

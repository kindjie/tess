# C5 dynamic congestion revalidation: retained evidence

Pre-registration: issue #256. Source under measurement: the C0 substrate
on main `b87900a5` plus the recorded program in `programs.md` -- no
library change exists in either arm, by design: the priced arm writes
the substrate's own `CostTag` field through the versioned edit channel
(`mark_content_changed` + `mark_pathing_dirty`), and the C0 `Traveler`
class already composes `FieldCost`, so the canonical planner simply
reads the new prices.

**Verdict: rejected -- the historical rejection is reconfirmed on the
corrected substrate.** The pre-registered parity gate (identical
per-seed terminal-classification multiset; any regression on any family
rejects) fails on 12 of 132 seeds, and the re-rejection rule fires
directly: the priced arm produces incomplete arrivals canonical does not
(warehouse t3 and t17 each lose an arrival to a seal; colony t1, t2, t4,
t11 likewise regress), which is precisely the failure mode that rejected
congestion pricing in the colony-demo era. Divergence runs in BOTH
directions -- colony t0/t3/t13/t15 and warehouse t4 improve -- which is
the finding: a bounded local price signal perturbs marginal seeds
chaotically rather than systematically, the same trajectory-divergence
mechanism C4's substrate measurement documented for escalation.

Aggregate failure counts barely move (warehouse 10 -> 11, colony 330 ->
328, random_dense 31 -> 30, everything else identical), and the tick
metric shows no value where classification agrees: pooled paired
geometric mean 0.9977, CI [0.9810, 1.0180] over the 115
identical-classification seeds -- the interval includes 1.0, so even
without the parity failures there is nothing to accept.

Gates that PASSED, for the record: bit-identical replay of the priced
arm on every seed (the canonical arm was NOT re-replayed here -- a
letter-gap against gate 3's "per arm" wording, noted rather than left
silent; canonical determinism is separately pinned by the substrate's
own rebuild-reproducibility test and by C4's per-seed replays); the scripted edit replay with pricing active
(warehouse trial 0, close/reopen a tile at ticks 32/96 through the
versioned channel) is deterministic, uncensored, and fully arrives --
pricing composes correctly with topology invalidation; the policy's
writes stay within the declared bound (cost in [1, 4]).

**What follows.** Pricing is re-rejected with the same signature as the
original rejection, now on pinned substrate seeds rather than demo
anecdotes. For PR C6's opening condition: this screen isolates NO
capacity-contention premise -- the divergences are chaotic
per-seed shifts, not systematic hotspot relief -- so C6 (capacity-aware
crossing reservation) dispositions without a run in X3, per the plan's
"without that changed premise" clause.

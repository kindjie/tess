## 2026-08-21 - Scope discrepancy experiments and name the RC-1 evaluation

- Recorded a discrepancy-aware allocation experiment program outside the
  pre-1.0 prototype queue. It does not gate `v1.0.0`, adds nothing to the
  bounded queue, and yields to the execution plan whenever the two contend
  for serialized controlled-hardware time. Its ordering is set by feasibility
  against this codebase and the available machines rather than by the order
  its source proposed.
- Demoted accumulated discrepancy from success criterion to diagnostic.
  A policy that minimizes a quantity cannot be judged primarily on that
  quantity; the same rule now applies to balance for a partitioner and to hit
  rate for a cache policy. Primary endpoints must be independent of the
  mechanism under test, and every accepted result carries a wrong-signal
  control.
- Required an offline counterfactual replay over retained artifacts before
  any selection-policy implementation. Where the incumbent order already
  shows tight staleness and deadline distributions, the program is discarded
  without new runtime machinery.
- Classified multi-domain server hardware as a best-effort target, secondary
  before 1.0 and open to promotion after it. It sits outside the
  cross-hardware decision rule, gains no calibrated gate or support-policy
  commitment, carries single-platform evidence labelled as such, and may
  neither pessimize nor destabilize the two supported platforms. Determinism
  is not relaxed for it.
- Deferred device-placement experiments because no operation currently has
  both a CPU and a GPU implementation between which placement could be a
  runtime choice. The reason is the empty operation set, not the memory
  architecture of the available machines.
- Recorded that the RC-1 downstream evaluation will use a co-developed
  reference consumer rather than an independent one, that the relationship
  belongs in the evidence so the result is not mistaken for independent
  validation, and that findings only an unfamiliar reader can produce fall
  outside its reach. The gate is judged on coverage of the listed surfaces,
  which currently omit pathfinding and path invalidation.

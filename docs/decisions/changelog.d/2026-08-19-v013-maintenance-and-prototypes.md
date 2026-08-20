## 2026-08-19 - Put stable maintenance in v0.13 before pre-RC prototypes

- Made stable maintenance handles, an external dense-and-sparse chunk adapter,
  cross-hardware evidence, a focused downstream tryout, and API graduation
  release gates for `v0.13.0`. FIFO and queued-coalescing backends remain
  experimental comparison machinery. Dirty-bit promotion additionally
  requires a portable performance win; a flat result may graduate the contract
  with immediate execution while keeping dirty-bit experimental. World
  construction and authoritative storage do not acquire an implicit scheduler.
- Scheduled the bounded pathfinding, movement, congestion, and execution
  prototype queue after 0.13 and before `v1.0.0-rc.1`. Each candidate may be
  accepted, rejected, or explicitly deferred, but every disposition must be
  recorded and every accepted implementation must land before downstream
  evaluation.
- Required paired M3 and Steam Deck evidence before a portable performance
  change is accepted or rejected on performance. One material win with no
  material regression on the other platform passes; correctness, contract,
  determinism, lifetime, or allocation failures may stop a run earlier.
- Applied the stable failure-diagnostics boundary to maintenance: capacity,
  idle, budget, stall, and task-failure outcomes remain explicit results;
  unsafe lifecycle or ownership misuse fails fast, while expected stale-handle
  uncertainty uses a checked operation.
- Kept controlled campaign evidence distinct from permanent CI authority.
  Hosted timing thresholds remain advisory until representative calibration
  establishes useful sensitivity and an acceptable false-positive rate.
- Recorded the complete ordering, dependencies, parallel streams, hardware
  requirements, evidence rules, downstream gate, RC checks, and GA observation
  in `docs/planning/v0.13-to-v1.0-execution-plan.md`.

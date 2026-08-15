## 2026-08-14 - Colony settled-goal enclosure and turnaround

- Area: the 128x128 browser colony at up to 1,024 agents, especially its eight
  destination columns and wall edits made while a leg is active.
- Diagnosis: the reported 968-arrived/56-terminal state had no moving agents.
  A `Traveler` exact `NoPath` excluded settled teammates, but the demo promoted
  that snapshot result to durable `Unreachable` and then published the failed
  agent as another settled obstacle. The all-arrived relaunch gate could never
  open. Separately, the wall setter admitted occupied tiles even though the
  movement and path APIs correctly reject an impassable source.
- Accepted demo policy: after settled-aware failure, an independent exact
  terrain-only search distinguishes a durable wall failure from a teammate-only
  enclosure. The latter cancels the unfinished goal and is quiescent for the
  current leg. Once all agents have arrived or are crowd-blocked, the
  controller aborts that leg and rearms the synchronized wave in the opposite
  direction. Completed and crowd-aborted legs are counted separately. Wall
  requests are rejected synchronously when their tile is occupied, and
  topology updates now run in `Pathing` before `Movement`. No core-library
  semantics changed.
- Rejected wake-up experiment: rearming each arrived agent after a 20-tick
  dwell made settled occupancy temporary, but introduced mixed-direction
  traffic. In the 1,024-agent narrow-gap control it still had not completed a
  leg after 5,000 ticks; the debug run took 87.7 seconds. Temporary sidestep
  goals were not attempted because they also require restoration, reservation,
  chain arbitration, and starvation policy.
- Deferred dynamic-routing option: the existing PIBT movement tier already
  supports an active agent stepping off-route and priority-inheriting through
  active teammates. Its ring, detour, passability-consistency, allocation, and
  immovable-arrived tests pass, but the mechanism cannot move an arrived or
  unreachable agent and therefore cannot repair the reported quiescent state.
  Exact per-agent ranking for 1,024 distinct goals would hold about 64 MiB of
  distance cells before metadata and would rebuild as settled passability
  changes. It remains a separate library-policy option if classified live
  congestion persists after the demo lifecycle fix.
- Evidence: native regressions cover occupied-wall rejection and retry after
  vacancy; a four-neighbour enclosure under retained and all-agent-replan
  strategies; a synthetic exact 968+56 state whose formerly blocked agents
  complete the recovery leg; 1,024 agents naturally crowd-sealed by a mid-leg
  wall before all eight destination columns; retained and all-agent-replan wall
  seals; a 128-agent two-wall bottleneck; and the existing 48-agent three-wall
  case. The maximum-scale turnaround keeps the shared retained-route planning
  budget at eight exact queries per tick. Broader path-agent, PIBT, and
  colony-harness suites cover serial/pool execution, worker counts, chunk
  sizes, cache states, and incremental/fresh topology.
- Performance boundary: normal movement and core library code are unchanged;
  occupied-wall admission is one field read, while a failed settled-aware
  recovery may need a deferred terrain confirmation within the existing exact
  query cap. The final O3 native self-check took 1.27 seconds; the Wasm bundle
  compiled, loaded in a web-runtime smoke, exposed the new counters through
  `cwrap`, and rejected an occupied wall tile. The self-check now covers
  substantially more scenarios than the 96 ms baseline and is not a paired
  hot-tick comparison. Accepted-policy retained-versus-replan timing therefore
  remains unmeasured rather than being claimed as a non-regression.

# Blocked-agent recovery scheduling

Status: **proposed** (2026-08-14).

## Problem

`PathAgentTickOptions::max_blocked_retries` currently converts an exhausted
movement wait into `PathAgentPhase::Unreachable` and `PathStatus::NoPath`, even
when the retained route remains geometrically valid. A large congested demo
worked around that false terminal verdict by running an independent exact
search for every blocked agent at the same retry count. At 1,024 agents the
cohort performs roughly 1.6 million node expansions in one browser frame.

Retry exhaustion is a liveness-policy event; `NoPath` is a reachability claim.
The tick driver does not have enough information to turn the former into the
latter. Permanent occupancy, transient occupancy, scripted blockers, sparse
residency, and settled actors require caller-visible distinctions.

## Contract

- Exhaustion policy is explicit. The compatibility policy may terminalize an
  agent, while the honest policy leaves it `Blocked`, preserves its last path
  status, and stops repeated route processing after the retry limit.
- A blocked agent that remains active continues attempting a retained
  occupancy- or reservation-blocked step. Successful movement resets both the
  retry streak and any recovery schedule episode.
- A world-dirty replan can recover a `Blocked` agent whose previous exact
  search returned `NoPath`; unlike `Unreachable`, it is not skipped.
- Recovery scheduling selects candidates; it never decides reachability. A
  caller resolves the selected start/goal pairs using the same movement class,
  transition provider, residency policy, and world snapshot as movement.
- Deferral cannot consume a terminal allowance. Only authoritative caller
  policy may convert a selected agent to a terminal state.

## Proposed responsibilities

`PathAgentTickOptions` gains an exhaustion-policy value. The current terminal
behavior remains selectable; the colony uses the non-terminal policy.

A caller-owned, index-paired recovery schedule observes agent phases and
returns a bounded span of due indices. Each completed check is acknowledged,
which schedules its next check with capped exponential backoff and
deterministic jitter. Progress or lifecycle exit ends the episode. The
schedule offers no wall-time promise: its query-count cap bounds herd width,
while a single exact search may still require a large synchronous flood.

The schedule is mutable scratch and follows the existing external-
synchronization convention. Selection happens on the frame-owner thread.
Selected read-only queries may be distributed by a caller only when each
worker owns its search scratch and results are applied deterministically after
all workers finish.

## Scenario boundaries

- **Small populations:** reserved warm storage makes collection allocation-
  free; the scan remains linear in the supplied agent span.
- **Large populations:** the per-tick selection cap is a hard query-count
  bound, and deterministic jitter prevents a common retry count from also
  becoming a common due tick.
- **Dense and sparse obstacles:** scheduling is storage-independent. Sparse
  `Indeterminate` remains non-terminal and must not be rewritten as `NoPath`.
- **Chunked worlds:** the resolver, not the schedule, owns chunk-version and
  movement-class consistency.
- **Multithreading:** no shared mutable global state or random generator is
  introduced. The schedule itself is not concurrently mutable.

## Alternatives

- Increasing the retry limit postpones the herd and false terminal verdict.
- Jitter without a hard budget improves average distribution but offers no
  worst-tick bound.
- A demo-local connected-component cache is attractive for its undirected
  unit-cost grid but cannot represent every library transition model.
- Resumable A* could bound individual-query work, but it is a larger search
  contract and is deferred until query budgeting proves insufficient.

## Verification

Unit tests cover legacy and non-terminal exhaustion, recovery after a world
change, deterministic selection, bounded work, episode reset, and warm
allocation behavior. The colony fixture supplies the scale regression.
Profiles compare work counters and tick distributions across population,
obstacle, storage, and executor-relevant cells. ThreadSanitizer verifies that
the new state introduces no hidden shared mutation.

Topology-triggered all-agent replanning is a separate design problem. This
work records its cost but does not weaken route invalidation or optimality to
hide it.

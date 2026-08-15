# Budgeted path-agent replanning

Status: proposed

## Problem

An authoritative world edit can legitimately invalidate every retained route.
The synchronous path-agent tick then solves the complete population in one
call. In the 1,024-agent colony workload, the 239-wall edit affects all 1,024
routes and produces a roughly 300 ms browser tick after blocked recovery is
otherwise bounded. Selective route invalidation cannot help that workload;
portal-first replanning measured roughly 615 ms because rejected candidates
paid both portal and exact-search costs.

## Contract

The library will provide a caller-owned FIFO of agent indices needing exact
replanning. Requests are deduplicated while pending. A processing call solves
at most a caller-specified number of requests, copies successful paths into the
existing retained-route storage, and applies the normal `Following`/`Blocked`
result lifecycle. The default missing-chunk policy remains explicit so sparse
callers can select `Indeterminate` rather than inventing `NoPath`.

This queue is an opt-in eventual-replanning mechanism. Existing synchronous
tick drivers and their exact all-at-once semantics do not change. Until a
queued agent is processed, the caller may retain its old route only where
movement validation rejects newly illegal steps; otherwise it must stop that
agent. A request budget bounds the number of searches, not the expansions or
wall time of one search. Resumable A* remains a separate future mechanism if a
single path query is itself too large.

## Ownership and scenarios

The queue, agents, retained routes, and search scratch are externally
synchronized simulation state. Separate owners can process independent queues
concurrently, but one queue is serial. FIFO order and caller-selected budgets
make results deterministic across dense, sparse, and chunked worlds. A budget
larger than a small population completes it in one call without changing path
semantics. Obstacle density changes individual search cost, not queue order.

## Verification

- Repeated all-agent requests deduplicate without changing FIFO order.
- Processing never exceeds the request budget and eventually drains fairly.
- Found and non-Found outcomes match direct exact search, including sparse
  `Indeterminate` when requested.
- Warm queue operations allocate nothing after caller reservation.
- The colony shares one per-tick exact-query budget between topology replans
  and blocked recovery, and its matched browser distribution is recorded.

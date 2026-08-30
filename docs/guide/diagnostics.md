---
title: Instrument a Real Colony Workload
description: >-
  Enable tess diagnostics around a deterministic colony, capture counters and
  traces on their recording thread, and interpret lifecycle flow accounting.
---

# Instrument a real colony workload

Diagnostics answer what one real workload did; they should not replace it with
a convenient synthetic loop. This tutorial compiles the shared 128×128 colony
model in a separate diagnostics-enabled host, records its queued wall edits,
bounded path planning, schedule timings, and goal lifecycle, then renders
read-only snapshots.

<iframe
  class="diagnostics-frame"
  src="../../demo/diagnostics/"
  title="Interactive colony diagnostics tutorial"
  loading="lazy">
  The diagnostics tutorial requires WebAssembly and WebGL2 support.
</iframe>

[Open the standalone diagnostics demo](../../demo/diagnostics/){ .md-button }

!!! info "API used"

    [`tess::diagnostics::ScopedPathCounters`][api-path-counters] ·
    [`tess::diagnostics::DiagnosticsSnapshot`][api-snapshot] ·
    [`tess::diagnostics::FlowAccounting`][api-flow-accounting] ·
    [`tess::diagnostics::FlowHealthSnapshot`][api-flow-health]

## Compile the whole host consistently

`TESS_ENABLE_DIAGNOSTICS` changes which counter types exist and which inline
instrumentation sites execute. Define it for the target, not for one source
file. The native self-check and WebAssembly host each compile both the
diagnostics adapter and `web_colony/colony_model.cc` under the same definition.
Shipping builds leave it undefined, so the counter and trace sites compile
away.

Dear ImGui remains consumer-owned. Only the browser artifact defines
`TESS_ENABLE_IMGUI`, fetches the pinned Dear ImGui source, and includes tess's
reference panels. Tess itself neither links nor fetches the UI toolkit.

## Attach lifecycle accounting before admission

The colony constructor accepts one example-local `FlowAccounting` pointer and
attaches it before `initialize_agents` admits the first goals. The model then
records admission, completion, cancellation, failure, and supersession at the
transition where each outcome becomes known. Reconstructing those histories
from the current agent array would lose the distinction between outcomes.

This is **lifecycle flow accounting**. It is not a pathfinding flow field:

- lifecycle flow accounting conserves admitted work through terminal and
  outstanding buckets;
- a pathfinding flow field retains per-tile movement directions toward a
  destination.

The demo shows both conservation identities:

```text
offered = admitted + rejected + coalesced
admitted = terminal outcomes + outstanding
```

It also shows the outstanding high-water mark, terminal outcomes, planning
work, and the current inventory. A broken identity is a correctness failure,
not a threshold warning.

## Capture on the recording thread

Path, queued-phase, allocation, and trace sinks are thread-local. The host
installs the path, queued-phase, and trace scopes around one colony tick. It
then installs the allocation scope separately around the consumer-owned
presentation snapshot. After both operations destroy their scopes, the host
calls `capture_diagnostics` and captures `FlowHealthSnapshot` at the same
quiescent point. The snapshots are plain values that the render pass may read
without touching a live sink.

The first tick proves the complete workload:

1. a queued wall edit executes and publishes dirty work;
2. topology and bounded path planning run through the colony schedule;
3. path and queued counters receive real events;
4. scheduler and task duration spans reach the trace buffer;
5. a consumer-owned presentation snapshot produces balanced allocation and
   deallocation events;
6. the goal accountant satisfies both conservation identities;
7. Dear ImGui submits a non-empty frame.

Pause freezes simulation ticks without fabricating new samples. Reset creates
a fresh colony and a fresh accounting history. Editing the selected tile is a
real queued colony operation, so the queued-work text changes only after the
next fixed tick applies it.

## Read allocation zero honestly

Allocation diagnostics are consumer-instrumented. Tess does not intercept
hidden allocations in an adopter's runtime. This demo records a bounded,
consumer-owned presentation snapshot during readiness; after capacity has
been reserved, ordinary warm ticks may correctly show zero allocations and
zero frees. Zero is evidence about the installed hooks and measured interval,
not a claim that every layer of the process allocated nothing.

## Other branches

- **Diagnostics off:** shipping builds need zero counter and trace surface.
- **Counters and traces:** development builds need complete workload evidence.
- **Dear ImGui host:** an interactive tool owns ImGui and renders snapshots.

World overview, chunk inspection, and boolean edit-intent helpers remain
available from `tess/debug/imgui/tools.h`. They accept const worlds and return
application-owned edit intents; they do not grant the panel mutation
authority.

For the complete contracts and thread-local limits, read the
[diagnostics architecture](../architecture/diagnostics.md).

[api-path-counters]: https://tess.owx.dev/api/classtess_1_1diagnostics_1_1ScopedPathCounters.html
[api-snapshot]: https://tess.owx.dev/api/structtess_1_1diagnostics_1_1DiagnosticsSnapshot.html
[api-flow-accounting]: https://tess.owx.dev/api/structtess_1_1diagnostics_1_1FlowAccounting.html
[api-flow-health]: https://tess.owx.dev/api/structtess_1_1diagnostics_1_1FlowHealthSnapshot.html

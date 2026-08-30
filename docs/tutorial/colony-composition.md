---
title: Compose a Deterministic Colony
description: >-
  Build a fixed-step colony from queued terrain edits, dirty topology,
  bounded planning, joint movement, and versioned DeltaFrame presentation.
---

# Compose a deterministic colony

The basic pathfinding tutorial answers one route query. A simulation has to
decide when edits become visible, how much planning work one tick may perform,
when movement commits, and how a renderer recovers if it misses a change.
This tutorial follows those decisions through the existing 128×128 colony
model. It does not change the model's coordinates, agent goals, or movement
semantics.

<iframe
  class="colony-frame"
  src="../../demo/colony/?presentation=article"
  title="Interactive colony composition tutorial"
  loading="lazy">
  The colony tutorial requires a browser with WebAssembly support.
</iframe>

[Open the full colony demo](../../demo/colony/){ .md-button }

The article view deliberately offers fewer controls. The standalone view keeps
the complete 1–1,024-agent controls, strategy switches, wall clearing, and
pointer painting. Both compile and run the same C++ model.

!!! info "API used"

    [`tess::AutoExecTask`][api-auto-exec] ·
    [`tess::Schedule`][api-schedule] ·
    [`tess::PathAgentReplanQueue`][api-replan-queue] ·
    [`tess::DeltaFrame`][api-delta-frame]

## The five-stage composition

One fixed tick has a single authority order:

1. admit queued world edits;
2. run a dirty-driven topology rebuild;
3. spend the bounded path planning budget;
4. perform the movement commit;
5. publish `DeltaFrame` presentation and recovery data.

That order is more important than any one task. Planning never observes a
half-applied wall, movement never precedes the resulting topology update, and
the renderer remains a consumer rather than another writer.

## 1. Admit queued world edits

Pointer input asks the model to build or erase a wall. Admission is immediate,
but accepted mutation waits for the schedule's `PreUpdate` phase. The model
deduplicates a pending edit, refuses to build over an occupied tile, and queues
one chunk-scoped operation. This gives every run the same edit/topology/move
order even though browser events arrive between animation frames.

<!-- tess-snippet: colony-queued-edits source=examples/web_colony/colony_model.cc -->
```cpp
auto ColonyModel::Impl::set_wall(tess::Coord3 coord, bool built) -> bool {
  // Example: queue a world edit. Admission is synchronous, but mutation is
  // deferred to the PreUpdate AutoExec task so dirty publication, topology,
  // and movement retain one deterministic schedule order.
  // JavaScript and the Wasm model run on one thread: no fixed tick can move
  // an agent between this admission check and the next PreUpdate build. Keep
  // the invariant every other colony writer already follows -- construction
  // never turns an occupied source into impassable terrain.
  const auto pending = std::find_if(
      pending_walls.begin(), pending_walls.end(),
      [coord](const WallEdit& edit) { return edit.coord == coord; });
  const auto effective = pending != pending_walls.end()
                             ? pending->built
                             : world.field<ConstructionTag>(coord) != 0;
  if (effective == built) {
    return true;
  }
  if (built && world.field<OccupancyTag>(coord)) {
    return false;
  }
  if (pending != pending_walls.end()) {
    pending->built = built;
    return true;
  }
  pending_walls.push_back(WallEdit{coord, built});
  const auto key = tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord));
  (void)ops.update_field(
      tess::DomainDesc::explicit_chunks({&key, 1}),
      tess::FieldAccessDesc{0, kTerrainDirty.value, kTerrainDirty},
      tess::WritePolicy::UniquePerChunk);
  return true;
}
```
<!-- /tess-snippet -->

The browser remembers only wall commands that the model admitted. That is
presentation convenience across reset, not permission for JavaScript to own
occupancy or terrain truth.

## 2. Rebuild topology only when dirty

The build task marks the terrain dirty. A `Pathing` task with an `OnDirty`
cadence then collects exactly the affected chunks and incrementally refreshes
the region graph. No terrain change means no topology work.

<!-- tess-snippet: colony-schedule-order source=examples/web_colony/colony_model.cc -->
```cpp
schedule.reserve_tasks(3);
(void)schedule.add_task(
    {"build", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
    *build_task);
(void)schedule.add_task({"topology", tess::SimPhase::Pathing,
                         tess::Cadence::on_dirty(kTerrainDirty)},
                        topology_task);
// Pathing follows PreUpdate in the same fixed tick, so walls built above
// refresh topology before the Movement task can plan against them.
(void)schedule.add_task(
    {"agents", tess::SimPhase::Movement, tess::Cadence::every_tick()},
    agent_task);
schedule.seal();
```
<!-- /tess-snippet -->

A topology revision also invalidates route-selection snapshots and requests
canonical replans. This is why dirty tracking is correctness state rather than
merely an optimization hint.

## 3. Bound planning work

New and invalidated goals enter caller-owned queues. Each fixed tick admits at
most eight exact queries across the optional route-spreading queues, canonical
replans, and recovery probes. A count budget does not cap the cost of one
search, but it prevents an unbounded number of searches from landing in one
tick.

Agents whose query has not reached the front of the FIFO wait with textual
`pending plans` state. The model never fabricates a path to keep the picture
moving.

## 4. Commit movement after planning

The movement task consumes retained routes only after the planning stage. Its
joint movement pass resolves chains and rotations together and deliberately
permits swaps for these point-like colonists. Occupancy and reservation writes
commit inside that pass.

The policy is part of this example's agent semantics, not a universal claim
about collision avoidance. A production game with physical bodies may forbid
swaps or add a separate local-avoidance layer.

## 5. Publish presentation and recover gaps

After scheduled ticks, the model collects dirty terrain into a versioned
`DeltaFrame`. A frame is invalidation: the consumer re-reads authoritative
world values for covered coordinates. If the consumer version cannot accept
an incremental frame, continuing with later deltas would preserve a gap, so
the model publishes a complete baseline instead.

<!-- tess-snippet: colony-delta-recovery source=examples/web_colony/colony_model.cc -->
```cpp
void ColonyModel::Impl::publish_render_frame() {
  tess::collect_tile_deltas(deltas, world, kTerrainDirty);
  if (consume_frame(deltas.publish())) {
    return;
  }

  // Example: recover a rejected DeltaFrame. A version gap or truncation is
  // structural, so skipping it and resuming incrementals cannot repair the
  // shadow. Publish a complete baseline and adopt its version instead.
  tess::collect_baseline(deltas, world, kTerrainDirty);
  if (!consume_frame(deltas.publish())) {
    TESS_ASSERT(false);
  }
}
```
<!-- /tess-snippet -->

Agent drawing uses a parallel presentation boundary. C++ snapshots the
previous and current integer-tile positions around each fixed tick and exports
the accumulator alpha. JavaScript interpolates those read-only endpoints onto
a responsive high-DPI canvas. Fractional coordinates are browser-only
presentation state: they never feed back into goals, occupancy, pathfinding,
or the next simulation tick.

## What to carry into your own host

Keep one explicit order from accepted writes to derived topology, bounded
planning, movement, and presentation. Give render consumers a version and a
baseline recovery path. Finally, separate smooth drawing from simulation
truth: interpolation can be discarded and recreated without changing the
colony outcome.

[api-auto-exec]: https://tess.owx.dev/api/classtess_1_1AutoExecTask.html
[api-schedule]: https://tess.owx.dev/api/classtess_1_1Schedule.html
[api-replan-queue]: https://tess.owx.dev/api/classtess_1_1PathAgentReplanQueue.html
[api-delta-frame]: https://tess.owx.dev/api/classtess_1_1DeltaFrame.html

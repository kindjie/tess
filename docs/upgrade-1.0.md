# Upgrade to tess 1.0

Version 0.13 is the final breaking-change release before 1.0. Update code to
the forms below before adopting the first 1.0 release candidate.

## Path requests

Path, reachability, coarse-path, precheck, and distance-field APIs now receive
one `PathRequest` instead of adjacent `start` and `goal` arguments.

<!-- tess-snippet: upgrade-path-request source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// auto result = tess::reachable<Shape>(graph, start, goal, graph_scratch);

// After:
const auto result = tess::reachable<Shape>(
    graph, tess::PathRequest{start, goal}, graph_scratch);
```
<!-- /tess-snippet -->

Apply the same wrapping to `coarse_path`, `precheck_path`,
`distance_field_path`, and `weighted_distance_field_path`. Direct A* already
used `PathRequest` and does not change.

## Options and handles

Route-cache limits are now named:

<!-- tess-snippet: upgrade-route-cache-limits source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// cache.set_caps(max_entries, max_path_nodes);

// After:
cache.set_caps(tess::RouteCacheLimits{max_entries, max_path_nodes});
```
<!-- /tess-snippet -->

Direct path-agent movement also uses a named options object:

<!-- tess-snippet: upgrade-agent-options source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// tess::advance_path_agents_with_movement<
//     World, PassableTag, OccupancyTag, ReservationTag>(
//     world, agents, runtime, max_steps, movement_dirty_mask);

// After:
const auto stats =
    tess::advance_path_agents_with_movement<World, PassableTag, OccupancyTag,
                                            ReservationTag>(
        world, agents, runtime,
        tess::PathAgentAdvanceOptions{max_steps, movement_dirty_mask});
```
<!-- /tess-snippet -->

The same `PathAgentAdvanceOptions` migration applies to the joint-movement and
PIBT direct-advance families. For tick APIs, put `movement_dirty_mask` inside
`PathAgentTickOptions` instead of passing a separate integer after the options
argument. `SimSchedulerOptions::movement_dirty_mask` was removed; migrate it to
the nested path-agent options:

<!-- tess-snippet: upgrade-scheduler-agent-options source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// tess::SimSchedulerOptions options{
//     .movement_dirty_mask = movement_dirty_mask,
// };

// After:
const auto scheduler_options = tess::SimSchedulerOptions{
    .path_agent_options =
        tess::PathAgentTickOptions{
            .movement_dirty_mask = movement_dirty_mask,
        },
};
```
<!-- /tess-snippet -->

GPU dispatch and readback descriptors now retain one generation-bearing
handle:

<!-- tess-snippet: upgrade-gpu-handle source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// tess::gpu::DispatchDesc dispatch{
//     .product_key = handle.key,
//     .product_generation = handle.generation,
// };

// After:
const auto dispatch = tess::gpu::DispatchDesc{.handle = handle};
```
<!-- /tess-snippet -->

The same replacement applies to `ReadbackDesc`.

## Parameter order

Output objects now precede scratch objects, and merge destinations precede
their partition sources:

<!-- tess-snippet: upgrade-parameter-order source=tests/tess_upgrade_1_0_test.cc -->
```cpp
// Before:
// tess::build_distance_field_product<World, PassableTag>(
//     world, goals, field_scratch, product);
// tess::merge_planned_dirty(world, partitions, dirty_scratch);
// tess::collect_render_tile_deltas(world, dirty_mask, render_deltas);

// After:
const auto field = tess::build_distance_field_product<World, PassableTag>(
    world, goals, product, field_scratch);
const auto merged =
    tess::merge_planned_dirty(world, dirty_scratch, partitions);
tess::collect_render_tile_deltas(render_deltas, world, dirty_mask);
```
<!-- /tess-snippet -->

`build_weighted_distance_field_product` and provider-aware field-product
overloads follow the same output-before-scratch order.

## Renamed and removed spellings

- Replace `pipeline.to_frontier(destination)` with the identical
  `pipeline.collect_into(destination)` terminal.
- Replace `trace.category_stats()` with `trace.all_stats()`. Per-category
  lookup remains `trace.stats(category)`. Replace
  `timing_snapshot.category(category)` with
  `timing_snapshot.stats(category)`.

## Lifetime and identity changes

`DeltaFrame` record spans are private. Use `chunks()`, `tiles()`, `entities()`,
`overlays()`, and `overlay_nodes()`. These accessors and `empty()` fail fast if
the collector publishes, reserves, moves, or is destroyed after producing the
view. Read or copy the records before any invalidating operation.

`ResumableWorkQueue` and `ResumableWorkTask` are no longer copyable or movable.
Construct registered tasks and their queues at their final stable addresses;
store them behind stable ownership when a container would otherwise relocate
them.

Worker-pool nested dispatch, concurrent dispatch on one pool, and reservation
during dispatch now fail fast in every build. Use distinct pool objects for
independent concurrent dispatches and reserve before publishing work.

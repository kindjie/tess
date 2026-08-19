---
title: A* vs Route Caches, Batches, and Distance Fields
description: >-
  Compare C++ grid pathfinding workloads using A*, route caching, weighted
  batches, and shared-goal distance fields in the tess C++20 library.
---

# A* vs route caches, batches, and distance fields

There is no universally fastest pathfinding API. The useful question is which
work repeats: a route, a goal, or nothing at all. tess exposes separate APIs
for those shapes so a caller can pay for reuse only when a measured workload
has reuse to exploit.

This comparison uses one 16x16 world and three requests. The
[complete self-checking example][strategy-source] compiles and runs in CI; each
excerpt below is copied from that source and rejected by CI if it drifts.
Timing evidence comes from the benchmark suite instead of from this teaching
program.

## The short answer

| Workload | Start with | What is reused |
| --- | --- | --- |
| One-off requests or mostly distinct goals | `astar_path` | Search scratch only |
| Exact or same-goal suffix routes repeat | `cached_astar_path` | Stored routes |
| Weighted requests arrive together | `weighted_path_batch` | Work within the batch |
| Many starts share one unit-cost goal | distance field | One reverse search tree |

Route caches, weighted batches, and the two-call distance-field API all support
dense and sparse-resident worlds. Persistent distance-field *products* are a
different, dense-only family. The
[pathfinding decision guide](guide/pathfinding.md) covers that residency
boundary and the full API selection tree.

## One world, one request set

The example gives every open tile unit passability and cost. That keeps the
returned costs comparable while the call shapes change.

<!-- tess-snippet: strategy-world source=examples/pathfinding_strategies.cc -->
```cpp
struct PassableTag {};
struct CostTag {};

using Shape = tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                 tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
```
<!-- /tess-snippet -->

<!-- tess-snippet: strategy-requests source=examples/pathfinding_strategies.cc -->
```cpp
constexpr auto kGoal = tess::Coord3{15, 15, 0};

constexpr auto kRequests = std::array{
    tess::PathRequest{tess::Coord3{0, 0, 0}, kGoal},
    tess::PathRequest{tess::Coord3{0, 1, 0}, kGoal},
    tess::PathRequest{tess::Coord3{0, 2, 0}, kGoal},
};
```
<!-- /tess-snippet -->

## One-off A*: the default

Plain A* is the baseline for a request that does not share useful work with
other requests. Reuse the scratch object between calls, but do not introduce a
cache or field until measurements show repeated structure.

<!-- tess-snippet: strategy-astar source=examples/pathfinding_strategies.cc -->
```cpp
tess::PathScratch scratch;
const auto result =
    tess::astar_path<World, PassableTag>(world, kRequests.front(), scratch);
```
<!-- /tess-snippet -->

Use it when goals are mostly distinct, the map changes too often for retained
routes to survive, or the request count is small enough that the direct call is
already below the application budget.

## Route cache: repeated paths on a stable map

`cached_astar_path` stores exact routes and same-goal suffixes. A first request
still performs A*; a repeat can return without expanding search nodes.

<!-- tess-snippet: strategy-cache source=examples/pathfinding_strategies.cc -->
```cpp
tess::PathScratch scratch;
tess::RouteCacheScratch cache;
const auto first = tess::cached_astar_path<World, PassableTag>(
    world, kRequests.front(), scratch, cache);
const auto repeated = tess::cached_astar_path<World, PassableTag>(
    world, kRequests.front(), scratch, cache);
```
<!-- /tess-snippet -->

The cache is caller-owned and bounded. Its world fingerprint invalidates stale
entries in exact mode; scoped feasibility is a deliberate alternative with a
different optimality contract. Read the
[route-cache specification](architecture/path.md) before choosing that policy.

## Weighted batch: group work arriving together

`weighted_path_batch` groups requests by goal. Repeated goals can share a
bounded weighted field; distinct goals fall back to per-request weighted A*.
The API therefore preserves one result per input request while choosing the
strategy inside the batch.

<!-- tess-snippet: strategy-batch source=examples/pathfinding_strategies.cc -->
```cpp
tess::WeightedPathBatchScratch scratch;
const auto results =
    tess::weighted_path_batch<World, PassableTag, CostTag, /*MaxCost=*/32>(
        world, kRequests, scratch);
```
<!-- /tess-snippet -->

Use the statistics (`field_builds`, `astar_fallbacks`, and `unique_goals`) to
verify that a real request set contains the reuse the batch was meant to find.

## Distance field: many starts, one goal

A reverse distance field builds one goal-rooted search tree. Every matching
start then reconstructs a path from that field instead of running another A*.

<!-- tess-snippet: strategy-distance-field source=examples/pathfinding_strategies.cc -->
```cpp
tess::DistanceFieldScratch scratch;
const auto field =
    tess::build_distance_field<World, PassableTag>(world, kGoal, scratch);
std::array<tess::PathResult, kRequests.size()> results{};
for (std::size_t index = 0; index < kRequests.size(); ++index) {
  results[index] = tess::distance_field_path<World, PassableTag>(
      world, kRequests[index], scratch);
}
```
<!-- /tess-snippet -->

The field is tied to its goal and world snapshot. Rebuild it after relevant
world or residency changes. For cross-frame, multi-goal retention on a dense
world, use the separate `DistanceFieldProduct` and `FieldProductCache` family.

## What the benchmarks compare

The benchmark suite contains paired workloads rather than timing unrelated
examples:

| Question | Baseline | Reuse strategy |
| --- | --- | --- |
| 100 starts share one room-and-portal goal | independent A* | one distance field |
| 100 requests contain exact repeats | independent A* | exact route cache |
| 100 starts lie on one route to a goal | independent A* | suffix route cache |
| 100 weighted requests share eight goals | weighted A* | weighted batch planner |

The pairs use the same world and request arrays inside each question. They
validate returned paths and expose counters such as unique goals, expanded
nodes, cache hits, and misses outside the timed loops. See the
[benchmark implementation][benchmark-source] and the
[performance methodology](performance.md) before treating a result as a
portable constant.

For a concrete scale check, these are median CPU times from a Release build on
one Apple M3 Max. Each benchmark ran single-threaded for at least one second
per repetition across ten repetitions:

| Workload | Independent searches | Reuse strategy | Relative time |
| --- | ---: | ---: | ---: |
| Shared unit-cost goal | 17.80 ms | 2.78 ms field | 6.4x faster |
| Exact route repeats | 48.88 ms | 14.52 ms cache | 3.4x faster |
| Same-goal suffixes | 113.08 us | 17.41 us cache | 6.5x faster |
| Eight weighted goals | 441.16 ms | 42.29 ms batch | 10.4x faster |

The run could not pin thread affinity and the system load average was about
4.0, so use these numbers as workload evidence, not target-machine promises.
The result that matters is the shape: reuse won substantially when the paired
request set actually contained the structure each API is designed to share.

Run the comparison on the target machine:

```sh
cmake --preset bench
cmake --build --preset bench --target tess_bench
./build/bench/bench/tess_bench \
  --benchmark_filter='path/.*batch_100_.*' \
  --benchmark_min_time=1s \
  --benchmark_repetitions=10 \
  --benchmark_report_aggregates_only=true
```

The decision is workload-specific: compare paired names with the same suffix,
inspect their counters, and retain the simpler API when the measured benefit
does not justify another invalidation or grouping lifecycle.

[strategy-source]: https://github.com/kindjie/tess/blob/main/examples/pathfinding_strategies.cc
[benchmark-source]: https://github.com/kindjie/tess/tree/main/bench

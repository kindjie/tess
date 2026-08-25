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
[complete self-checking example][strategy-main] compiles and runs in CI; each
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

## Algorithm and strategy status

The four choices in the short answer are not four peer algorithms. A* and the
reverse field builders perform graph search; caches, batches, and retained
products decide when to reuse that work; movement coordination resolves tile
conflicts after routes have been planned.

<div class="strategy-status-table" markdown="1">

| Layer | Capability | Status and boundary |
| --- | --- | --- |
| Search | Unit-cost A\* and weighted A\* | <span class="strategy-status strategy-status--released">Released</span> Exact, deterministic per-request routes over orthogonal, diagonal, and axial-hex movement models. |
| Search | Reverse BFS, reverse Dijkstra, and bounded-cost bucket search | <span class="strategy-status strategy-status--released">Released</span> Shared-goal distance labels: regular unit-cost models use BFS, weighted or non-unit models use Dijkstra, and small bounded integer costs can use an exact Dial-style queue. |
| Reuse | Exact/suffix route cache, weighted batch, and field-product cache | <span class="strategy-status strategy-status--released">Released</span> Workload policies layered over the searches above; they do not introduce another route-quality objective. |
| Topology | Reachability precheck, coarse region/portal routes, and chunk corridors | <span class="strategy-status strategy-status--released">Released</span> Coarse products can rule out known disconnection or guide exact segments; the automatic chunk-portal builder does not claim globally optimal portal selection. |
| Coordination | Joint movement and PIBT | <span class="strategy-status strategy-status--released">Released</span> Resolve contention between independently planned routes. They are movement algorithms, not globally optimal multi-agent pathfinding. |
| Future fields | Flow, congestion, and influence products | <span class="strategy-status strategy-status--designed">Designed, not shipped</span> The [roadmap](roadmap.md#future-and-deferred-extensions) keeps these explicit; today, callers can express congestion through a weighted cost field. |
| Application layer | Continuous steering, formations, and globally optimal multi-agent planning | <span class="strategy-status strategy-status--boundary">Out of scope</span> tess supplies the spatial substrate while applications retain these semantics. |

</div>

The [path architecture](architecture/path.md) specifies the search and reuse
contracts. The [simulation architecture](architecture/simulation.md) separates
route planning from joint movement and PIBT.

<details class="strategy-alternatives" markdown="1">
<summary>Why some alternatives were not promoted</summary>

These decisions are deliberately scoped. A rejected browser policy or
internal data structure is not a rejection of the broader research idea.

- **Four-ary open-list heap** —
  <span class="strategy-status strategy-status--rejected">Experiment rejected</span>
  It helped a few A* benchmark cases but substantially regressed weighted field builds
  and failed the second-platform non-regression gate
  ([evidence][quad-heap-rejection]).
- **Dynamic congestion prices in the colony demo** —
  <span class="strategy-status strategy-status--released">Validated caller recipe</span>
  The original policies produced incomplete arrivals and were rejected
  ([evidence][congestion-rejection]); a later revalidation on the corrected
  topology superseded that result — a bounded demand-driven price policy
  retained or improved terminal classification on every supported population
  across all seven demo scenarios on two platforms, and is documented as a
  caller recipe with its measured boundary
  ([spatial coordination](architecture/spatial-coordination.md),
  [evidence][congestion-revalidation]). No library mechanism was added.
- **Balanced gate waypoints** —
  <span class="strategy-status strategy-status--rejected">Experiment rejected</span>
  Assigning equal cohorts to exact crossing tiles synchronized agents onto
  capacity hotspots, increased waits, and lost arrivals
  ([evidence][waypoint-rejection]).
- **Eight-step WHCA-style space-time planning** —
  <span class="strategy-status strategy-status--boundary">Not promoted</span>
  The screening result was 30–90x the cheap resolver's per-tick cost and
  degraded at dense bottlenecks whose queues exceeded the fixed horizon
  ([screening study][movement-screening]).

The pre-RC screens in the [execution plan][execution-plan] rejected all
three of the remaining classical candidates on retained evidence:
gated 4-connected JPS regressed dense rubble maps despite large
structured-map wins ([evidence][jps-rejection]); whole-query
bidirectional A\* confirmed material regressions on five of eight cells
with correctness gates green ([evidence][bidir-rejection]); and goal-keyed
D\* Lite rejected at feasibility once both arms were counted with the
same ruler — its repair machinery does essentially the same abstract
work as searching fresh against this incumbent
([evidence][dstar-rejection]). Each record carries its scoped
reconsideration condition; none is a verdict on other domains.
Theta\* remains deferred until its supporting contracts exist.

</details>

## See the call shapes

The embedded demo runs the same C++ model as the self-checking example. Its
animation shows call order and reuse; it deliberately does not time
WebAssembly or draw a search frontier that the APIs do not report.
The obstacle course is shared, and comparable requests return the same routes.
The cache card instead repeats its first request to expose reuse. Compare the
operation chain and data-product label above each map to see whether tess
repeats searches, retains a route, groups a batch, or labels the reachable map
for later reads.

<iframe class="strategy-demo-frame"
        src="../demo/strategies/"
        loading="lazy"
        title="Interactive pathfinding strategy comparison"></iframe>

[Open the strategy demo in a separate page](../demo/strategies/).

## One world, one request set

The example uses three solid vertical walls with alternating single-tile gaps.
Every passable tile has unit cost, so each strategy must solve the
same visible obstacle course and the returned costs remain comparable. The
demo model copies each borrowed path before the next scratch mutation so the
browser reads read-only C++ result snapshots.

<!-- tess-snippet: strategy-world source=examples/pathfinding_strategies_model.cc -->
```cpp
struct PassableTag {};
struct CostTag {};
using WeightedMovement =
    tess::movement::PositiveCostFieldMovement<PassableTag, CostTag>;

using Shape = tess::Shape<tess::Extent3{16, 16}, tess::Extent3{8, 8}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                 tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
```
<!-- /tess-snippet -->

<!-- tess-snippet: strategy-obstacles source=examples/pathfinding_strategies_model.cc -->
```cpp
[[nodiscard]] constexpr auto demo_tile_passable(std::int64_t x, std::int64_t y)
    -> bool {
  if (x == 4) {
    return y == 4;
  }
  if (x == 8) {
    return y == 11;
  }
  if (x == 12) {
    return y == 6;
  }
  return true;
}
```
<!-- /tess-snippet -->

<!-- tess-snippet: strategy-requests source=examples/pathfinding_strategies_model.cc -->
```cpp
constexpr auto kGoal = tess::Coord2{15, 15};

constexpr auto kRequests = std::array{
    tess::PathRequest{tess::Coord2{0, 0}, kGoal},
    tess::PathRequest{tess::Coord2{0, 1}, kGoal},
    tess::PathRequest{tess::Coord2{0, 2}, kGoal},
};
```
<!-- /tess-snippet -->

## Independent A*: the default

Plain A* is the baseline when requests do not share useful work. The example
runs all three requests independently and reuses only scratch storage; it does
not reuse search results. Do not introduce a cache or field until measurements
show repeated structure.

<!-- tess-snippet: strategy-astar source=examples/pathfinding_strategies_model.cc -->
```cpp
tess::PathScratch scratch;
for (std::size_t index = 0; index < kRequests.size(); ++index) {
  const auto result =
      tess::astar_path<World, PassableTag>(world, kRequests[index], scratch);
  snapshot.requests[index] = copy_result(result);
}
```
<!-- /tess-snippet -->

Use it when goals are mostly distinct, the map changes too often for retained
routes to survive, or the request count is small enough that the direct call is
already below the application budget.

## Route cache: repeated paths on an unchanged map

`cached_astar_path` stores exact routes and same-goal suffixes. A first request
still performs A*; a repeat can return without expanding search nodes.

<!-- tess-snippet: strategy-cache source=examples/pathfinding_strategies_model.cc -->
```cpp
tess::PathScratch scratch;
tess::UnitRouteCache cache;
const auto first = tess::cached_astar_path<World, PassableTag>(
    world, kRequests.front(), scratch, cache);
snapshot.requests[0] = copy_result(first);
const auto repeated = tess::cached_astar_path<World, PassableTag>(
    world, kRequests.front(), scratch, cache);
snapshot.requests[1] = copy_result(repeated);
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

<!-- tess-snippet: strategy-batch source=examples/pathfinding_strategies_model.cc -->
```cpp
tess::WeightedPathBatchScratch scratch;
const auto results =
    tess::weighted_path_batch<World, WeightedMovement, /*MaxCost=*/32>(
        world, kRequests, scratch);
for (std::size_t index = 0; index < results.size(); ++index) {
  snapshot.requests[index] = copy_result(results[index]);
}
```
<!-- /tess-snippet -->

Use the statistics (`field_builds`, `astar_fallbacks`, and `unique_goals`) to
verify that a real request set contains the reuse the batch was meant to find.

## Distance field: many starts, one goal

A reverse distance field builds one goal-rooted search tree. Every matching
start then reconstructs a path from that field instead of running another A*.

<!-- tess-snippet: strategy-distance-field source=examples/pathfinding_strategies_model.cc -->
```cpp
tess::DistanceFieldScratch scratch;
const auto field =
    tess::build_distance_field<World, PassableTag>(world, kGoal, scratch);
for (std::size_t index = 0; index < kRequests.size(); ++index) {
  const auto result = tess::distance_field_path<World, PassableTag>(
      world, kRequests[index], scratch);
  snapshot.requests[index] = copy_result(result);
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
  --benchmark_filter='path/.*batch(_planner)?_100_.*' \
  --benchmark_min_time=1s \
  --benchmark_repetitions=10 \
  --benchmark_report_aggregates_only=true
```

The decision is workload-specific: compare paired names with the same suffix,
inspect their counters, and retain the simpler API when the measured benefit
does not justify another invalidation or grouping lifecycle.

[strategy-source]: https://github.com/kindjie/tess/blob/main/examples/pathfinding_strategies_model.cc
[strategy-main]: https://github.com/kindjie/tess/blob/main/examples/pathfinding_strategies.cc
[benchmark-source]: https://github.com/kindjie/tess/tree/main/bench
[quad-heap-rejection]: https://github.com/kindjie/tess/blob/main/docs/planning/optimization-log.d/2026-08-10-quad-heap-rejected.md
[congestion-rejection]: https://github.com/kindjie/tess/blob/main/docs/planning/optimization-log.d/2026-08-17-colony-wide-merge-second-wave.md
[congestion-revalidation]: https://github.com/kindjie/tess/blob/main/docs/planning/evidence/v1.0/c5-congestion/README.md
[jps-rejection]: https://github.com/kindjie/tess/blob/main/docs/planning/evidence/v1.0/p3-jps/README.md
[bidir-rejection]: https://github.com/kindjie/tess/blob/main/docs/planning/evidence/v1.0/p4-bidir/README.md
[dstar-rejection]: https://github.com/kindjie/tess/blob/main/docs/planning/evidence/v1.0/p5-dstar/README.md
[waypoint-rejection]: https://github.com/kindjie/tess/blob/main/docs/planning/optimization-log.d/2026-08-17-colony-balanced-waypoints-rejected.md
[movement-screening]: https://github.com/kindjie/tess/blob/main/docs/planning/local-movement-resolution.md#evidence
[execution-plan]: https://github.com/kindjie/tess/blob/main/docs/planning/v0.13-to-v1.0-execution-plan.md

---
title: Stream a Procedural Sparse World
description: >-
  Stream deterministic 32×32 chunks through a bounded 32-page Tess resident
  set while agents retry routes at unavailable-data boundaries.
---

# Stream a procedural sparse world

This tutorial models a **large and bounded** 4,096×4,096 tile world. It is
not infinite: its 128×128 chunk grid has known edges, even though only a small
part exists in memory at once. Terrain is generated in 32×32 chunks from a
fixed seed and chunk coordinates.

The live model keeps a 32-page sparse resident set. Its camera asks for a
deduplicated 5×5 camera window, so the normal required set is 25 pages. Four
agents and their local goals stay inside the inner 3×3 chunks; they therefore
do not expand that set.

!!! info "API used"

    [`tess::World`][api-world] specialized with `SparseResident` owns the
    fixed-budget least-recently-used set. [`tess::astar_path`][api-astar]
    searches only resident pages, and `MissingChunkPolicy::ReportIndeterminate`
    distinguishes unavailable data from a proven missing route.

<iframe class="sparse-stream-frame"
  src="../../demo/sparse-stream/"
  title="Interactive procedural sparse-stream tutorial">
  <p>Your browser cannot embed this example.
    <a href="../../demo/sparse-stream/">Open the sparse-stream example in a
    separate page</a>.</p>
</iframe>

[Open the sparse-stream example in a separate page](../../demo/sparse-stream/).

The blue outline is the current required window. Green pages were newly
generated at the most recent camera boundary, dark-blue pages were retained,
and pink outlines show pages the existing least-recently-used policy evicted.
The model starts paused; **Start**, **Pause**, and **Reset** are ordinary DOM
controls. Reduced-motion visitors get an explicit **Step** control instead of
automatic animation.

## Deterministic terrain with fixed crossings

Each new page is generated only from the world seed and global tile
coordinate. Every chunk has a passable horizontal and vertical centre line,
creating fixed cross-chunk corridor openings. A coordinate hash adds obstacles
away from those corridors.

That split is useful for a first streaming model: regeneration is interesting,
but the camera-following route remains easy to verify. The native self-check
evicts one page, regenerates the same chunk with the same seed, and compares
every field byte. The regenerated values must be byte-identical.

## Fail before mutation

Capacity is checked against the complete required union before `touch()` or
`ensure_resident()` runs. If the union exceeds capacity, the model reports a
capacity status and must **fail before mutation**. A native fixture constructs
the same protocol with a 24-page capacity; its 25-page request fails while the
resident count remains zero.

This is back-pressure at a clean boundary. Partially applying a window would
make the result depend on iteration order and could evict useful pages before
the model knows it cannot satisfy the request.

## One safe residency order

Residency mutation invalidates borrowed page references, spans, resident-key
views, and slot indices. The model therefore uses one explicit order:

1. snapshot the previous resident keys;
2. touch already-resident required pages first;
3. materialize missing required pages;
4. generate only newly materialized pages;
5. finish and classify every residency mutation;
6. reacquire spans and references, then begin pathfinding.

<!-- tess-snippet: sparse-stream-residency-order source=examples/web_sparse_stream/sparse_stream_model.cc -->
```cpp
const auto previous_span = impl_->world.resident_chunk_keys();
const std::vector<tess::ChunkKey> previous_resident(previous_span.begin(),
                                                    previous_span.end());
if (impl_->required.size() > impl_->world.capacity()) {
  impl_->stream_status = StreamStatus::CapacityExceeded;
  return impl_->stream_status;
}

for (const auto key : impl_->required) {
  if (impl_->world.is_resident(key)) {
    (void)impl_->world.touch(key);
  }
}

auto materialized = false;
for (const auto key : impl_->required) {
  if (!impl_->world.is_resident(key)) {
    if (!materialized) {
      impl_->newly_generated.clear();
      impl_->evicted.clear();
      materialized = true;
    }
    (void)impl_->world.ensure_resident(key);
    impl_->newly_generated.push_back(key);
  }
}
for (const auto key : impl_->newly_generated) {
  generate_chunk(impl_->world, key, impl_->seed);
}

impl_->retained.clear();
for (const auto key : previous_resident) {
  if (impl_->world.is_resident(key)) {
    impl_->retained.push_back(key);
  } else if (materialized) {
    impl_->evicted.push_back(key);
  }
}

impl_->refresh_view();
for (std::size_t index = 0; index < impl_->agents.size(); ++index) {
  auto& agent = impl_->agents[index];
  if (agent.position == agent.goal) {
    agent.status = AgentStatus::AtGoal;
    impl_->choose_next_goal(agent);
  }
  const auto result = tess::astar_path<World, PassableTag>(
      impl_->world, tess::PathRequest{agent.position, agent.goal},
      impl_->scratch[index], tess::MissingChunkPolicy::ReportIndeterminate);
  if (result.status == tess::PathStatus::Found && result.path.size() > 1) {
    agent.position = tess::Coord2{result.path[1].x, result.path[1].y};
    agent.status = agent.position == agent.goal ? AgentStatus::AtGoal
                                                : AgentStatus::Moving;
  } else if (result.status == tess::PathStatus::Found) {
    agent.status = AgentStatus::AtGoal;
  } else {
    agent.status = AgentStatus::Waiting;
  }
}
```
<!-- /tess-snippet -->

There is no explicit eviction call. Once the fixed resident set is full,
`ensure_resident()` selects a least-recently-used victim. Touching required
pages before adding missing pages keeps the entire required window ahead of
non-required candidates in that policy.

## Unavailable is not unreachable

An agent route can meet a non-resident page. With
`ReportIndeterminate`, Tess reports `PathStatus::Indeterminate` instead of
claiming `NoPath`. The agent waits textually and retries after the next
streaming pass. The native fixture first searches across a missing bridge
chunk, then materializes it and proves that the retry finds the route.

Normal goals remain inside the resident halo, so the live agents progress.
This keeps the example focused on residency mechanics rather than pretending
that a short local query solves long paths crossing unavailable regions.

## What a production streamer adds

The synchronous generator is deliberately small. A production system usually
adds **asynchronous generation** or I/O, **persistence of edits**,
**predictive prefetching**, **separate rendering and simulation radii**,
page **pinning**, explicit **back-pressure**, and policies for **long paths
crossing unavailable regions**.

The first version keeps persistent edits out of scope: evicting a chunk
discards changes, so the deterministic generator recreates the baseline only.
This tutorial also keeps collision avoidance separate. The four agents move
independently through global tiles; residency and route availability do not
arbitrate simultaneous occupancy.

[api-world]: https://tess.owx.dev/api/classtess_1_1World.html
[api-astar]: https://tess.owx.dev/api/classtess_1_1PathScratch.html#a7b7d735ab95ab0db2275b679188873b4

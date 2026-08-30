---
title: Flow-style Steering from Distance Labels
description: >-
  Build one tess distance product and steer independent agents by reading
  deterministic next steps from its public distance labels.
---

# Flow-style steering from distance labels

When many agents share one destination, calculating a complete path for each
agent repeats useful work. A single `DistanceFieldProduct` labels every
reachable tile with its unit-cost distance to the goal. Each agent can then
choose its next step on demand.

This tutorial uses a dense, unit-cost, orthogonal 32×24 world. The native
self-check and the browser view run the same C++ model. The presentation
starts paused; choose **Start**, a goal preset, or a passable tile in the grid.

!!! info "API used"

    [`DistanceFieldProduct`](https://tess.owx.dev/api/classtess_1_1DistanceFieldProduct.html)
    retains public distance labels for dense worlds. The model builds it with
    `build_distance_field_product()` and reads labels with `distance_at()`.

<iframe class="flow-steering-frame"
  src="../../demo/flow-steering/"
  title="Interactive flow-style steering tutorial">
  <p>Your browser cannot embed this example.
    <a href="../../demo/flow-steering/">Open the steering example in a
    separate page</a>.</p>
</iframe>

[Open the steering example in a separate page](../../demo/flow-steering/).

The two agents starting on the same tile overlap deliberately. A shared field
provides global guidance; it does not coordinate occupancy.

## Build once, read at each step

The model gives `build_distance_field_product()` one goal. Changing the goal
rebuilds the product synchronously before another agent step is allowed. A
label of zero means the agent is at the goal. The unreachable sentinel means
there is no route through the currently passable topology. Both states hold
the agent and appear in the textual status beneath the canvas.

For a unit-cost orthogonal world, a legal move must have a distance exactly
one less than the current cell. Merely choosing the smallest neighbouring
number would hide the invariant this example is meant to teach.

The compiled model uses north, east, south, then west as its fixed direction
order. The first legal descent wins, giving deterministic tie-breaking when
several shortest continuations exist:

<!-- tess-snippet: flow-steering-descent source=examples/web_flow_steering/flow_steering_model.cc -->
```cpp
for (auto& agent : impl_->agents) {
  const auto current_distance =
      impl_->product.distance_at<World>(agent.position);
  if (current_distance == 0) {
    agent.state = AgentState::AtGoal;
    continue;
  }
  if (current_distance == tess::DistanceFieldProduct::unreachable_distance) {
    agent.state = AgentState::Unreachable;
    continue;
  }

  auto descended = false;
  for (const auto direction : kDirectionOrder) {
    const auto neighbor = tess::Coord2{
        agent.position.x + direction.x,
        agent.position.y + direction.y,
    };
    if (!in_bounds(static_cast<int>(neighbor.x),
                   static_cast<int>(neighbor.y)) ||
        impl_->world.field<PassableTag>(neighbor) == 0) {
      continue;
    }
    const auto neighbor_distance =
        impl_->product.distance_at<World>(neighbor);
    if (neighbor_distance == current_distance - 1) {
      agent.position = neighbor;
      agent.state =
          neighbor_distance == 0 ? AgentState::AtGoal : AgentState::Moving;
      ++moved;
      descended = true;
      break;
    }
  }
  if (!descended) {
    agent.state = AgentState::Unreachable;
  }
}
```
<!-- /tess-snippet -->

This is on-demand next-step selection, not complete-path reconstruction. The
agent stores only its current cell and state. A complete path remains useful
when a consumer needs to inspect, reserve, serialize, or compare the whole
route before movement begins.

## Distance labels are not retained directions

A retained direction field stores the chosen outgoing direction at every
cell. That saves neighbour reads during movement, but consumes additional
memory and bakes one tie policy into the product. Retaining directions becomes
worthwhile when very large agent counts repeatedly read an unchanged field
and profiling shows that next-step selection matters.

`DistanceFieldProduct` deliberately retains distance labels instead. Its
retained-product boundary is dense worlds: the product reserves storage for
the whole shape. Sparse or streamed worlds should not treat this example as a
promise that all distant labels can remain materialized.

## Weighted costs change the equality

The “one less” rule depends on every move costing one. In a weighted world,
a valid descent satisfies the entry-cost Bellman equality:

`current_distance = entry_cost(neighbour) + neighbour_distance`

Use the same cost convention that built the product; do not simply choose the
smallest label. That equality proves the selected edge lies on a minimum-cost
continuation.

## Guidance is only one layer

This example moves independent agents and permits overlap. Production movement
may add **reservations** for future occupancy, **congestion** costs for route
choice, **collision avoidance** for simultaneous moves, and **local steering**
for continuous motion. Those systems can consume the same global guidance,
but none is supplied by a distance product itself.

---
title: Grid Pathfinding and Simulation Use Cases
description: >-
  Apply tess grid pathfinding, tile storage, and deterministic simulation to
  games, robotics prototypes, agent-based models, and headless servers.
---

# Use cases

The [getting-started tutorial](getting-started.md) teaches the concept
ladder in game terms. This page walks the same shipped machinery through
three other adopter frames, each backed by an existing self-checking
example.

## Robotics prototyping: occupancy grids and replanning

A tess world with a passability field is an occupancy grid, and the
dirty-driven loop is a replanner. `examples/stairs_3d.cc` demonstrates the
full cycle in under two hundred lines: build a two-level world joined by a
stair transition, verify reachability with the
[topology precheck](architecture/topology.md), then demolish the stair —
a direct field write, followed by handing that tile's chunk key to
`tess::update_region_graph`, which refreshes only the affected chunks, so
the next query correctly reports the goal unreachable.

Supplying that chunk key is the caller's job, and the example is showing
the discipline rather than boilerplate: a field write does not make a
region graph stale by itself, so a graph that is not told about the edit
keeps answering from the pre-edit snapshot. See
[field edits do not make a graph stale](architecture/topology.md).

For the queued-operations and cadence machinery named below, see
`examples/colony_2d.cc`, which composes them in a `tess::Schedule` loop.

Mapped to robotics vocabulary:

- **Occupancy update** — a field edit paired with the chunks it dirtied,
  not a full-map rewrite.
- **Replan trigger** — `Cadence::on_dirty(mask)` runs the planner exactly
  when the map changed.
- **Feasibility gate** — the precheck rejects definitively unreachable
  goals without expanding the grid; only `Unreachable` is trusted, so it
  never wrongly fails a solvable query.
- **Reproducibility** — ticks are fixed-step and deterministic: the same
  edits in the same order produce the same plans, which makes experiment
  runs repeatable and regressions bisectable.

The library is single-process and grid-based: it complements, rather than
replaces, continuous-space planners and ROS-style middleware.

## Agent-based modeling: many agents, shared fields

`examples/ant_farm_vertical.cc` is an agent-based model wearing game
clothes: a vertical cross-section world where a colony of ants shares one
multi-goal distance field through the byte-budgeted `FieldProductCache`
instead of searching independently. The same shape serves evacuation,
foraging, and diffusion-style studies:

- **Arbitrary per-tile state** — a [field schema](getting-started.md)
  holds whatever the model needs (pheromone, hazard, capacity), not just
  passability.
- **Population-scale routing** — agents sharing a goal set amortize one
  field build; the [pathfinding note](architecture/path.md) maps each
  workload shape to its API.
- **Determinism** — identical seeds and schedules reproduce identical
  runs, so results are citable and diffable.

## Headless simulation: servers and batch runs

Nothing in the core loop needs a window. A server or batch experiment
runs the [schedule](architecture/simulation.md) under its own fixed-step
clock and simply omits the render bridge, which is optional. When
observers do exist (a network mirror, a monitoring UI), `DeltaFrame`
versioning gives them gap detection and explicit resynchronization:
frames record what changed (individual tiles or box-granular dirty
bounds), and the consumer re-reads those tiles from the authoritative
world — or ships the values over its own channel — instead of rescanning
the map.
`examples/render_delta_consumer.cc` shows a consumer maintaining shadow
state this way.

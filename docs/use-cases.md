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
a queued edit marks the region dirty, `tess::update_region_graph`
refreshes only the affected chunks, and the next query correctly reports
the goal unreachable.

Mapped to robotics vocabulary:

- **Occupancy update** — a queued field edit with a dirty mask, not a
  full-map rewrite.
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
clock and simply omits the render bridge — it is the only fully optional
stage of the pipeline. When observers do exist (a network mirror, a
monitoring UI), `DeltaFrame` versioning gives them gap detection and
resynchronization without ever walking the world;
`examples/render_delta_consumer.cc` shows a consumer rebuilding shadow
state from published frames alone.

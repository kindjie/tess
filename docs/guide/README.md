# Choose your architecture

The [getting-started ladder](../getting-started.md) teaches the concepts
in order; the [Concepts pages](../architecture/README.md) specify them.
This guide does neither — it routes. Each page below is one architectural
decision: the question, the branches, the criteria that pick one, and
links to the rung that teaches it and the note that specifies it. When a
page names a default, pick it until measurement says otherwise.

```mermaid
flowchart TB
  accTitle: Decision-guide route map
  accDescr: Routes a project from memory budget through write path, path
    strategy, topology, entities, and presentation decisions.

  Start["What are you building?"] --> Mem{"Full world resident
within memory budget?"}
  Mem -->|"Yes - small or dense"| Dense["AlwaysResidentWorld"]
  Mem -->|"No - huge or mostly empty"| Sparse["SparseResidentWorld
+ byte budget"]
  Dense --> Edit{"Edits during simulation?"}
  Sparse --> Edit
  Edit -->|"Setup only"| Direct["Direct field writes"]
  Edit -->|"Sim-time"| Queued["FrameOps -> plan -> execute"]
  Direct --> Load{"Path workload shape?"}
  Queued --> Load
  Load -->|"Few one-off queries"| AStar["astar_path"]
  Load -->|"Costs / unit rules"| Weighted["MovementClass +
weighted_astar_path"]
  Load -->|"Many agents,
shared goals"| FieldP["distance field +
FieldProductCache"]
  Load -->|"Weighted batches,
repeated goals"| Batch["weighted_path_batch"]
  Load -->|"Repeated routes,
stable map"| Route["cached_astar_path"]
  AStar --> Pre{"Unreachable goals common?"}
  Weighted --> Pre
  FieldP --> Pre
  Batch --> Pre
  Route --> Pre
  Pre -->|"Yes"| Graph["RegionGraph + precheck,
OnDirty rebuild"]
  Pre -->|"No"| NoPre["Skip the precheck"]
  Graph --> Own{"Who owns agents?"}
  NoPre --> Own
  Own -->|"Plain structs"| NoEcs["PathAgentState directly"]
  Own -->|"EnTT"| Entt["EnTT adapter"]
  Own -->|"Flecs"| Flecs["Flecs adapter"]
  Own -->|"In-house ECS"| Custom["Custom adapter concepts"]
  NoEcs --> Show{"Observer on another cadence?"}
  Entt --> Show
  Flecs --> Show
  Custom --> Show
  Show -->|"Yes"| Deltas["DeltaCollector ->
versioned DeltaFrames"]
  Show -->|"No"| Headless["Headless - no render bridge"]
```

Jump to a decision:

- [Residency](residency.md) — can the whole world stay resident?
- [Writes](writes.md) — direct writes or queued operations?
- [Pathfinding strategy](pathfinding.md) — which path API fits the
  workload shape?
- [Topology and precheck](topology.md) — is a region graph worth its
  upkeep?
- [Entities and ECS](entities.md) — who owns the agents?
- [Presentation](presentation.md) — render bridge or headless?
- [Diagnostics](diagnostics.md) — which builds compile the
  instrumentation in?

One discipline keeps these pages honest: the guide decides, the
[ladder](../getting-started.md) teaches, and the
[Concepts pages](../architecture/README.md) specify. Semantics live in
the concept notes; if a sentence here would change when an API changes,
it belongs there instead. Features that exist only as prototypes or
designs are marked as such and never sit on a recommended branch — the
[roadmap](../roadmap.md) is the authoritative list.

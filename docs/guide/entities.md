# Entities and ECS

**The decision:** who owns the agents? tess never does — tiles are not
entities, and the substrate holds only path-agent lifecycle state. When
unsure, start with plain structs; the adapter seam exists precisely so
this choice can change later.

## Branches

| Branch | Pick when | Example |
| --- | --- | --- |
| No ECS — plain structs | small agent counts, no existing ECS; the adapter layer would be pure overhead | `examples/path_agents.cc` |
| EnTT adapter (`TESS_ENABLE_ENTT`) | you are already on EnTT, or want a maintained registry integration | `examples/entt_pawns.cc` |
| Flecs adapter (`TESS_ENABLE_FLECS`) | you use Flecs and need the same lifecycle and occupancy contract | `examples/flecs_pawns.cc` |
| Custom adapter | you have an in-house ECS or engine-side store | `examples/custom_ecs_min.cc` |

## The seam

Whatever the branch: deterministic-order agents in, state write-back
out. The [ECS note](../architecture/ecs.md) specifies the two contracts
every adapter must uphold — the path-agent lifecycle stays in the
substrate, and iteration order must be deterministic.

## Learn and specify

- Teach: the two adapter examples above; `custom_ecs_min.cc` proves the
  concepts on a deliberately non-EnTT-shaped micro ECS.
- Specify: [ECS note](../architecture/ecs.md) — adapter concepts,
  ordering requirements, occupancy synchronization.

Both maintained ECS adapters use the same generic pipeline. Their native
iteration order is deliberately ignored: each collects and sorts by the
monotonic `AgentId` component before submitting work.

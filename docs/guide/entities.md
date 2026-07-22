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
| Custom adapter | you have an in-house ECS or engine-side store | `examples/custom_ecs_min.cc` |

## The seam

Whatever the branch: deterministic-order agents in, state write-back
out. Ticket lifecycle — retries, exactly-once results — always lives in
the substrate's path-agent state; adapters mirror it, never reimplement
it. Iterate agents in monotonic id order, not pool order, or determinism
breaks.

## Learn and specify

- Teach: the two adapter examples above; `custom_ecs_min.cc` proves the
  concepts on a deliberately non-EnTT-shaped micro ECS.
- Specify: [ECS note](../architecture/ecs.md) — adapter concepts,
  ordering requirements, occupancy synchronization.

## Horizon

!!! note "Planned"
    A Flecs adapter is designed but not shipped (see the
    [roadmap](../roadmap.md)); the custom-adapter concepts are the
    supported path for any non-EnTT store today.

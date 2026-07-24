#pragma once

#include <tess/sim/path_agent.h>
#include <tess/sim/time.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace tess {

/// Owns the shared clock, world-dirty flag, and retained routes across ticks.
struct PathAgentTickState {
  SimClock clock{};
  // WORLD-scoped pathing dirt: set it (via mark_pathing_dirty) after any
  // world change that can invalidate existing routes; the next tick then
  // replans EVERY agent. Agent-scoped needs (a newly armed goal, a Blocked
  // retry) do not set it -- those agents alone replan while Following
  // agents keep walking their retained routes (audit/optimization-log
  // per-agent pathing-dirty item). Starts true so the first tick plans
  // everyone.
  bool pathing_dirty = true;
  // Per-agent retained routes; see PathAgentRoutes for the index-pairing
  // contract (reorder/remove agents => mark_pathing_dirty).
  PathAgentRoutes routes{};
};

/// Configures per-tick movement, caching, and blocked-agent retry limits.
struct PathAgentTickOptions {
  std::size_t max_steps = 1;
  PathRuntimeCachePolicy cache_policy{};
  // Budget of consecutive ticks spent retrying a Blocked agent. Occupied and
  // reserved destinations retry the retained step without an occupancy-blind
  // search; route-invalidating failures re-path. The first movement failure
  // records the block, and each following tick consumes one attempt until a
  // successful move resets the count or exhaustion becomes Unreachable.
  std::uint32_t max_blocked_retries = 8;
};

/// Summarizes path planning and movement performed during one tick.
struct PathAgentTickStats {
  std::uint64_t tick = 0;
  bool processed_paths = false;
  PathAgentFrameStats pathing{};
  PathAgentFrameStats movement{};
  // Actual route-invalidating retries that requested path processing.
  std::size_t repaths_requested = 0;
  // Historical name retained for source compatibility: counts every agent
  // whose shared blocked budget exhausted, including retained-step waits.
  std::size_t repath_exhausted = 0;
};

/// Requests a full replan after a world-scoped pathing change.
inline void mark_pathing_dirty(PathAgentTickState& state) noexcept {
  state.pathing_dirty = true;
}

// Arms a goal WITHOUT touching the world-scoped dirty flag: the agent
// enters NeedsPath, which the next tick picks up as an agent-scoped
// (NeedsOnly) processing pass. Before the per-agent split this marked the
// shared flag and one new goal replanned the whole batch every tick
// (optimization-log 2026-07-11, S11.4 soak observation).
/// Arms one agent goal without forcing unrelated following agents to replan.
inline void set_path_agent_goal(PathAgentTickState& state,
                                PathAgentState& agent, Coord3 goal) noexcept {
  static_cast<void>(state);
  set_path_agent_goal(agent, goal);
}

// Scans agents ahead of a tick's path processing. NeedsPath agents request
// processing with no manual dirty mark. Blocked agents consume one retry per
// following tick. A retained Found route waits without path processing for
// occupancy/reservations; invalid routes request a re-path. Exhausted agents
// become terminally Unreachable and stop both processing and movement.
/// Advances retry accounting and reports whether any agent needs planning.
inline auto prepare_path_agent_processing(std::span<PathAgentState> agents,
                                          PathAgentTickOptions options,
                                          PathAgentTickStats& stats) noexcept
    -> bool {
  bool needs_processing = false;
  for (auto& agent : agents) {
    if (!agent.has_goal) {
      continue;
    }
    if (agent.phase == PathAgentPhase::NeedsPath) {
      needs_processing = true;
      continue;
    }
    if (agent.phase != PathAgentPhase::Blocked) {
      continue;
    }
    if (options.max_steps == 0) {
      continue;
    }
    if (agent.blocked_retries < options.max_blocked_retries) {
      ++agent.blocked_retries;
      if (agent.status != PathStatus::Found) {
        ++stats.repaths_requested;
        needs_processing = true;
      }
    } else {
      agent.phase = PathAgentPhase::Unreachable;
      agent.status = PathStatus::NoPath;
      ++stats.repath_exhausted;
    }
  }
  return needs_processing;
}

/// Advances one unit-cost path-agent tick without world movement validation.
template <typename World, typename ClassOrTag>
[[nodiscard]] auto tick_unit_path_agents(
    PathAgentTickState& state, const World& world,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    PathAgentTickOptions options = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, state.routes, options.max_steps);
  return stats;
}

/// Advances a provider-composed unit-cost tick without movement validation.
template <typename World, typename ClassOrTag, typename Provider>
[[nodiscard]] auto tick_unit_path_agents(
    PathAgentTickState& state, const World& world,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    PathAgentTickOptions options,
    const RegionGraphT<typename World::residency_type>* graph,
    const Provider& provider) -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes, provider);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, state.routes, options.max_steps);
  return stats;
}

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
/// Advances one unit-cost tick using validated occupancy movement commits.
[[nodiscard]] auto tick_unit_path_agents_with_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathAgentTickOptions options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement =
      advance_path_agents_with_movement<World, ClassOrTag, OccupancyTag,
                                        ReservationTag>(
          world, agents, state.routes, options.max_steps, movement_dirty_mask);
  return stats;
}

/// Advances a provider-composed unit tick through validated movement commits.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Provider>
[[nodiscard]] auto tick_unit_path_agents_with_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathAgentTickOptions options,
    std::uint32_t movement_dirty_mask,
    const RegionGraphT<typename World::residency_type>* graph,
    const Provider& provider) -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes, provider);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement =
      advance_path_agents_with_movement<World, ClassOrTag, OccupancyTag,
                                        ReservationTag>(
          world, agents, state.routes, options.max_steps, movement_dirty_mask,
          provider);
  return stats;
}

// Class forms: one movement class drives pathing, precheck, and (for the
// movement variant) commit validation, so plan and commit provably agree.
/// Advances one bounded weighted path-agent tick without movement commits.
template <typename World, typename Class, std::uint32_t MaxCost>
[[nodiscard]] auto tick_weighted_path_agents(
    PathAgentTickState& state, const World& world,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    PathAgentTickOptions options = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_weighted_path_agents<World, Class, MaxCost>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, state.routes, options.max_steps);
  return stats;
}

/// Advances a provider-composed bounded weighted tick without commits.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename Provider>
[[nodiscard]] auto tick_weighted_path_agents(
    PathAgentTickState& state, const World& world,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    PathAgentTickOptions options,
    const RegionGraphT<typename World::residency_type>* graph,
    const Provider& provider) -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_weighted_path_agents<World, Class, MaxCost>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes, provider);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, state.routes, options.max_steps);
  return stats;
}

template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag>
/// Advances one bounded weighted tick using validated movement commits.
[[nodiscard]] auto tick_weighted_path_agents_with_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathAgentTickOptions options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_weighted_path_agents<World, Class, MaxCost>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents_with_movement<World, Class, OccupancyTag,
                                                     ReservationTag>(
      world, agents, state.routes, options.max_steps, movement_dirty_mask);
  return stats;
}

/// Advances a provider-composed weighted tick through movement commits.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag, typename Provider>
[[nodiscard]] auto tick_weighted_path_agents_with_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathAgentTickOptions options,
    std::uint32_t movement_dirty_mask,
    const RegionGraphT<typename World::residency_type>* graph,
    const Provider& provider) -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing = process_weighted_path_agents<World, Class, MaxCost>(
        world, agents, runtime, options.cache_policy, graph, scope,
        &state.routes, provider);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents_with_movement<World, Class, OccupancyTag,
                                                     ReservationTag>(
      world, agents, state.routes, options.max_steps, movement_dirty_mask,
      provider);
  return stats;
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
/// Advances a weighted tick using separate legacy passability and cost tags.
[[nodiscard]] auto tick_weighted_path_agents(
    PathAgentTickState& state, const World& world,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    PathAgentTickOptions options = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing =
        process_weighted_path_agents<World, PassableTag, CostTag, MaxCost>(
            world, agents, runtime, options.cache_policy, graph, scope,
            &state.routes);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, state.routes, options.max_steps);
  return stats;
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost, typename OccupancyTag, typename ReservationTag>
/// Advances a legacy-tag weighted tick with validated movement commits.
[[nodiscard]] auto tick_weighted_path_agents_with_movement(
    PathAgentTickState& state, World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathAgentTickOptions options = {},
    std::uint32_t movement_dirty_mask = 0,
    const RegionGraphT<typename World::residency_type>* graph = nullptr)
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  const bool repath_needed =
      prepare_path_agent_processing(agents, options, stats);
  state.routes.ensure_size(agents.size());
  if (state.pathing_dirty || repath_needed) {
    const auto scope =
        state.pathing_dirty ? PathSubmitScope::All : PathSubmitScope::NeedsOnly;
    stats.pathing =
        process_weighted_path_agents<World, PassableTag, CostTag, MaxCost>(
            world, agents, runtime, options.cache_policy, graph, scope,
            &state.routes);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement =
      advance_path_agents_with_movement<World, PassableTag, OccupancyTag,
                                        ReservationTag>(
          world, agents, state.routes, options.max_steps, movement_dirty_mask);
  return stats;
}

}  // namespace tess

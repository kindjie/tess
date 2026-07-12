#pragma once

#include <tess/sim/path_agent.h>
#include <tess/sim/time.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace tess {

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

struct PathAgentTickOptions {
  std::size_t max_steps = 1;
  PathRuntimeCachePolicy cache_policy{};
  // Budget of consecutive failed re-path attempts for a Blocked agent.
  // Exactly one attempt is consumed per processed tick spent Blocked, by
  // prepare_path_agent_processing; blocked movement steps do not consume
  // budget themselves. A Found re-path result resets the count to zero
  // (apply_path_agent_results), so only consecutive non-Found results
  // exhaust the budget and turn the agent terminally Unreachable. An
  // agent whose re-paths keep planning Found but whose next step stays
  // blocked -- e.g. a permanently parked agent on the route, since
  // occupancy is not planning passability -- therefore re-paths every
  // tick indefinitely by design and never becomes Unreachable.
  std::uint32_t max_blocked_retries = 8;
};

struct PathAgentTickStats {
  std::uint64_t tick = 0;
  bool processed_paths = false;
  PathAgentFrameStats pathing{};
  PathAgentFrameStats movement{};
  std::size_t repaths_requested = 0;
  std::size_t repath_exhausted = 0;
};

inline void mark_pathing_dirty(PathAgentTickState& state) noexcept {
  state.pathing_dirty = true;
}

// Arms a goal WITHOUT touching the world-scoped dirty flag: the agent
// enters NeedsPath, which the next tick picks up as an agent-scoped
// (NeedsOnly) processing pass. Before the per-agent split this marked the
// shared flag and one new goal replanned the whole batch every tick
// (optimization-log 2026-07-11, S11.4 soak observation).
inline void set_path_agent_goal(PathAgentTickState& state,
                                PathAgentState& agent, Coord3 goal) noexcept {
  static_cast<void>(state);
  set_path_agent_goal(agent, goal);
}

// Scans agents ahead of a tick's path processing. NeedsPath agents (goals
// assigned through the two-argument set_path_agent_goal) request processing
// with no manual dirty mark. Blocked agents consume one re-path attempt per
// processed tick until the retry budget runs out, at which point they turn
// terminally Unreachable and stop requesting processing.
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
    if (agent.blocked_retries < options.max_blocked_retries) {
      ++agent.blocked_retries;
      ++stats.repaths_requested;
      needs_processing = true;
    } else {
      agent.phase = PathAgentPhase::Unreachable;
      agent.status = PathStatus::NoPath;
      ++stats.repath_exhausted;
    }
  }
  return needs_processing;
}

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

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
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

// Class forms: one movement class drives pathing, precheck, and (for the
// movement variant) commit validation, so plan and commit provably agree.
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

template <typename World, typename Class, std::uint32_t MaxCost,
          typename OccupancyTag, typename ReservationTag>
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

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
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

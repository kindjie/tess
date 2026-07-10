#pragma once

#include <tess/sim/path_agent.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace tess {

struct SimClock {
  std::uint64_t tick = 0;
};

struct PathAgentTickState {
  SimClock clock{};
  bool pathing_dirty = true;
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

inline auto advance_sim_tick(SimClock& clock) noexcept -> std::uint64_t {
  ++clock.tick;
  return clock.tick;
}

inline void mark_pathing_dirty(PathAgentTickState& state) noexcept {
  state.pathing_dirty = true;
}

inline void set_path_agent_goal(PathAgentTickState& state,
                                PathAgentState& agent, Coord3 goal) noexcept {
  set_path_agent_goal(agent, goal);
  mark_pathing_dirty(state);
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
  if (state.pathing_dirty || repath_needed) {
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, runtime, options.max_steps);
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
  if (state.pathing_dirty || repath_needed) {
    stats.pathing = process_unit_path_agents<World, ClassOrTag>(
        world, agents, runtime, options.cache_policy, graph);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement =
      advance_path_agents_with_movement<World, ClassOrTag, OccupancyTag,
                                        ReservationTag>(
          world, agents, runtime, options.max_steps, movement_dirty_mask);
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
  if (state.pathing_dirty || repath_needed) {
    stats.pathing = process_weighted_path_agents<World, Class, MaxCost>(
        world, agents, runtime, options.cache_policy, graph);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, runtime, options.max_steps);
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
  if (state.pathing_dirty || repath_needed) {
    stats.pathing = process_weighted_path_agents<World, Class, MaxCost>(
        world, agents, runtime, options.cache_policy, graph);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents_with_movement<World, Class, OccupancyTag,
                                                     ReservationTag>(
      world, agents, runtime, options.max_steps, movement_dirty_mask);
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
  if (state.pathing_dirty || repath_needed) {
    stats.pathing =
        process_weighted_path_agents<World, PassableTag, CostTag, MaxCost>(
            world, agents, runtime, options.cache_policy, graph);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, runtime, options.max_steps);
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
  if (state.pathing_dirty || repath_needed) {
    stats.pathing =
        process_weighted_path_agents<World, PassableTag, CostTag, MaxCost>(
            world, agents, runtime, options.cache_policy, graph);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement =
      advance_path_agents_with_movement<World, PassableTag, OccupancyTag,
                                        ReservationTag>(
          world, agents, runtime, options.max_steps, movement_dirty_mask);
  return stats;
}

}  // namespace tess

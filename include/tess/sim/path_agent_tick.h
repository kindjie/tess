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
};

struct PathAgentTickStats {
  std::uint64_t tick = 0;
  bool processed_paths = false;
  PathAgentFrameStats pathing{};
  PathAgentFrameStats movement{};
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

template <typename World, typename PassableTag>
[[nodiscard]] auto tick_unit_path_agents(PathAgentTickState& state,
                                         const World& world,
                                         std::span<PathAgentState> agents,
                                         PathRequestRuntime& runtime,
                                         PathAgentTickOptions options = {})
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  if (state.pathing_dirty) {
    stats.pathing = process_unit_path_agents<World, PassableTag>(
        world, agents, runtime, options.cache_policy);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, runtime, options.max_steps);
  return stats;
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
[[nodiscard]] auto tick_weighted_path_agents(PathAgentTickState& state,
                                             const World& world,
                                             std::span<PathAgentState> agents,
                                             PathRequestRuntime& runtime,
                                             PathAgentTickOptions options = {})
    -> PathAgentTickStats {
  PathAgentTickStats stats;
  stats.tick = advance_sim_tick(state.clock);

  if (state.pathing_dirty) {
    stats.pathing =
        process_weighted_path_agents<World, PassableTag, CostTag, MaxCost>(
            world, agents, runtime, options.cache_policy);
    stats.processed_paths = true;
    state.pathing_dirty = false;
  }

  stats.movement = advance_path_agents(agents, runtime, options.max_steps);
  return stats;
}

}  // namespace tess

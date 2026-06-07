#pragma once

#include <tess/path/path_runtime.h>
#include <tess/sim/movement.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace tess {

struct PathAgentState {
  Coord3 position{};
  Coord3 goal{};
  PathTicket ticket{};
  std::size_t path_index = 0;
  PathStatus status = PathStatus::NoPath;
  bool has_goal = false;
};

struct PathAgentFrameStats {
  std::size_t submitted = 0;
  std::size_t completed = 0;
  std::size_t found = 0;
  std::size_t invalid_start = 0;
  std::size_t invalid_goal = 0;
  std::size_t no_path = 0;
  std::size_t advanced = 0;
  std::size_t arrived = 0;
  MovementFailureCounts movement_failures{};
};

inline void set_path_agent_goal(PathAgentState& agent, Coord3 goal) noexcept {
  agent.goal = goal;
  agent.path_index = 0;
  agent.status = PathStatus::NoPath;
  agent.has_goal = true;
}

inline void clear_path_agent_goal(PathAgentState& agent) noexcept {
  agent.goal = {};
  agent.ticket = {};
  agent.path_index = 0;
  agent.status = PathStatus::NoPath;
  agent.has_goal = false;
}

inline auto submit_path_agents(std::span<PathAgentState> agents,
                               PathRequestRuntime& runtime)
    -> PathAgentFrameStats {
  PathAgentFrameStats stats;
  runtime.clear_requests();

  for (auto& agent : agents) {
    agent.path_index = 0;
    if (!agent.has_goal) {
      continue;
    }
    if (agent.position == agent.goal) {
      clear_path_agent_goal(agent);
      ++stats.arrived;
      continue;
    }
    agent.ticket = runtime.submit(PathRequest{agent.position, agent.goal});
    ++stats.submitted;
  }

  return stats;
}

inline void record_path_agent_status(PathAgentFrameStats& stats,
                                     PathStatus status) noexcept {
  switch (status) {
    case PathStatus::Found:
      ++stats.found;
      return;
    case PathStatus::InvalidStart:
      ++stats.invalid_start;
      return;
    case PathStatus::InvalidGoal:
      ++stats.invalid_goal;
      return;
    case PathStatus::NoPath:
      ++stats.no_path;
      return;
  }
}

inline auto apply_path_agent_results(std::span<PathAgentState> agents,
                                     const PathRequestRuntime& runtime)
    -> PathAgentFrameStats {
  PathAgentFrameStats stats;

  for (auto& agent : agents) {
    if (!agent.has_goal || agent.position == agent.goal) {
      continue;
    }

    const auto result = runtime.result(agent.ticket);
    agent.status = result.status;
    agent.path_index = 0;
    ++stats.completed;
    record_path_agent_status(stats, result.status);
  }

  return stats;
}

inline auto advance_path_agents(std::span<PathAgentState> agents,
                                const PathRequestRuntime& runtime,
                                std::size_t max_steps = 1)
    -> PathAgentFrameStats {
  PathAgentFrameStats stats;
  if (max_steps == 0) {
    return stats;
  }

  for (auto& agent : agents) {
    if (!agent.has_goal || agent.status != PathStatus::Found) {
      continue;
    }

    const auto result = runtime.result(agent.ticket);
    if (result.status != PathStatus::Found || result.path.empty()) {
      continue;
    }

    for (std::size_t step = 0; step < max_steps; ++step) {
      if (agent.path_index + 1 >= result.path.size()) {
        break;
      }
      ++agent.path_index;
      agent.position = result.path[agent.path_index];
      ++stats.advanced;
      if (agent.position == agent.goal) {
        clear_path_agent_goal(agent);
        agent.status = PathStatus::Found;
        ++stats.arrived;
        break;
      }
    }
  }

  return stats;
}

template <typename World, typename PassableTag, typename OccupancyTag,
          typename ReservationTag>
inline auto advance_path_agents_with_movement(
    World& world, std::span<PathAgentState> agents,
    const PathRequestRuntime& runtime, std::size_t max_steps = 1,
    std::uint32_t movement_dirty_mask = 0) -> PathAgentFrameStats {
  PathAgentFrameStats stats;
  if (max_steps == 0) {
    return stats;
  }

  for (auto& agent : agents) {
    if (!agent.has_goal || agent.status != PathStatus::Found) {
      continue;
    }

    const auto result = runtime.result(agent.ticket);
    if (result.status != PathStatus::Found || result.path.empty()) {
      continue;
    }

    for (std::size_t step = 0; step < max_steps; ++step) {
      if (agent.path_index + 1 >= result.path.size()) {
        break;
      }

      const auto from = agent.position;
      const auto to = result.path[agent.path_index + 1];
      const auto movement =
          commit_movement_intent<World, PassableTag, OccupancyTag,
                                 ReservationTag>(
              world, MovementIntent{from, to, {}}, movement_dirty_mask);
      if (movement.status != MovementStatus::Moved) {
        record_movement_failure(stats.movement_failures, movement.status);
        agent.status = PathStatus::NoPath;
        break;
      }

      ++agent.path_index;
      agent.position = to;
      ++stats.advanced;
      if (agent.position == agent.goal) {
        clear_path_agent_goal(agent);
        agent.status = PathStatus::Found;
        ++stats.arrived;
        break;
      }
    }
  }

  return stats;
}

inline void add_path_agent_stats(PathAgentFrameStats& lhs,
                                 PathAgentFrameStats rhs) noexcept {
  lhs.submitted += rhs.submitted;
  lhs.completed += rhs.completed;
  lhs.found += rhs.found;
  lhs.invalid_start += rhs.invalid_start;
  lhs.invalid_goal += rhs.invalid_goal;
  lhs.no_path += rhs.no_path;
  lhs.advanced += rhs.advanced;
  lhs.arrived += rhs.arrived;
  lhs.movement_failures.invalid += rhs.movement_failures.invalid;
  lhs.movement_failures.blocked += rhs.movement_failures.blocked;
  lhs.movement_failures.occupied += rhs.movement_failures.occupied;
  lhs.movement_failures.reserved += rhs.movement_failures.reserved;
  lhs.movement_failures.stale_version += rhs.movement_failures.stale_version;
  lhs.movement_failures.stale_topology += rhs.movement_failures.stale_topology;
}

template <typename World, typename PassableTag>
[[nodiscard]] auto process_unit_path_agents(const World& world,
                                            std::span<PathAgentState> agents,
                                            PathRequestRuntime& runtime,
                                            PathRuntimeCachePolicy policy = {})
    -> PathAgentFrameStats {
  auto stats = submit_path_agents(agents, runtime);
  (void)runtime.template process_unit_cached<World, PassableTag>(world, policy);
  add_path_agent_stats(stats, apply_path_agent_results(agents, runtime));
  return stats;
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
[[nodiscard]] auto process_weighted_path_agents(
    const World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathRuntimeCachePolicy policy = {})
    -> PathAgentFrameStats {
  auto stats = submit_path_agents(agents, runtime);
  (void)runtime
      .template process_weighted_batch<World, PassableTag, CostTag, MaxCost>(
          world, policy);
  add_path_agent_stats(stats, apply_path_agent_results(agents, runtime));
  return stats;
}

}  // namespace tess

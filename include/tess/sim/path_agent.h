#pragma once

#include <tess/path/path_runtime.h>
#include <tess/path/precheck.h>
#include <tess/sim/movement.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tess {

// Lifecycle of a path agent, decoupled from the last PathStatus result:
// - Idle: no goal (or arrived); the agent does not consume processing.
// - NeedsPath: a goal was assigned and no route has been computed yet.
// - Following: a Found route is being walked tile by tile.
// - Blocked: the last step or path attempt hit a transient failure; the agent
//   retries a retained occupancy-blocked step or re-paths an invalid route
//   until its shared retry budget runs out.
// - Unreachable: the retry budget was exhausted or a structural movement
//   failure occurred; terminal until a new goal is assigned.
/// Describes the lifecycle phase of an independently routed agent.
enum class PathAgentPhase : std::uint8_t {
  Idle,
  NeedsPath,
  Following,
  Blocked,
  Unreachable,
};

/// Stores one agent's goal, route cursor, and retry lifecycle state.
struct PathAgentState {
  Coord3 position{};
  Coord3 goal{};
  PathTicket ticket{};
  std::size_t path_index = 0;
  PathStatus status = PathStatus::NoPath;
  PathAgentPhase phase = PathAgentPhase::Idle;
  bool has_goal = false;
  std::uint32_t blocked_retries = 0;
};

/// Summarizes path submission, results, movement, and failure outcomes.
struct PathAgentFrameStats {
  std::size_t submitted = 0;
  std::size_t completed = 0;
  std::size_t found = 0;
  std::size_t invalid_start = 0;
  std::size_t invalid_goal = 0;
  std::size_t no_path = 0;
  // Sparse worlds: the search stopped at the resident-set boundary without
  // ruling out a route through a non-resident chunk
  // (PathStatus::Indeterminate).
  std::size_t indeterminate = 0;
  std::size_t cost_overflow = 0;
  // Agents whose goal an optional topology precheck proved unreachable before
  // A* (a subset of no_path). See PathRuntimeStats::precheck_ruled_out.
  std::size_t precheck_ruled_out = 0;
  std::size_t advanced = 0;
  std::size_t arrived = 0;
  std::size_t blocked_waits = 0;
  MovementFailureCounts movement_failures{};
};

// Which agents a processing pass (re)submits (per-agent pathing dirt,
// optimization-log 2026-07-11/12):
// - All: every agent with a goal replans -- required after a WORLD change,
//   which can invalidate any existing route.
// - NeedsOnly: only agents that cannot advance without a plan (NeedsPath or a
//   route-invalidated Blocked state); Following and occupancy-waiting agents
//   keep their retained routes. One agent arming a goal no longer replans the
//   whole batch.
/// Selects whether a processing pass replans all or only waiting agents.
enum class PathSubmitScope : std::uint8_t {
  All,
  NeedsOnly,
};

// Per-agent route retention, index-paired with the agents span handed to
// the tick drivers: routes[i] is agents[i]'s current Found route (empty
// otherwise). Retention is what makes PathSubmitScope::NeedsOnly sound --
// the runtime rebuilds its result storage every processing pass, so a
// non-resubmitted agent's route must live here. Vectors keep their
// capacity across replans, so warm ticks stay allocation-free.
//
// CONTRACT: the pairing is by span index. A caller that reorders, removes,
// or compacts its agents between ticks must call mark_pathing_dirty on the
// tick state (forcing one full replan) or keep routes[] in sync itself.
/// Owns index-paired route copies retained across scoped processing passes.
struct PathAgentRoutes {
  std::vector<std::vector<Coord3>> routes;

  void ensure_size(std::size_t count) {
    if (routes.size() < count) {
      routes.resize(count);
    }
  }
};

namespace detail {

// Occupancy and reservations are deliberately absent from path passability.
// A new search would return the same route, so these failures should retry the
// retained next step. Other transient commit failures invalidate the route and
// need path processing before movement can resume.
[[nodiscard]] inline bool movement_block_can_retry_route(
    MovementStatus status) noexcept {
  return status == MovementStatus::Occupied ||
         status == MovementStatus::Reserved;
}

inline void block_path_agent(PathAgentState& agent,
                             MovementStatus status) noexcept {
  agent.phase = PathAgentPhase::Blocked;
  if (!movement_block_can_retry_route(status)) {
    agent.status = PathStatus::NoPath;
  }
}

inline void resume_path_agent(PathAgentState& agent) noexcept {
  agent.phase = PathAgentPhase::Following;
  agent.blocked_retries = 0;
}

[[nodiscard]] inline bool can_skip_scoped_path_submission(
    const PathAgentState& agent) noexcept {
  return agent.phase == PathAgentPhase::Following ||
         (agent.phase == PathAgentPhase::Blocked &&
          agent.status == PathStatus::Found);
}

}  // namespace detail

/// Arms `agent` to plan a route toward `goal` on the next processing pass.
inline void set_path_agent_goal(PathAgentState& agent, Coord3 goal) noexcept {
  agent.goal = goal;
  agent.path_index = 0;
  agent.status = PathStatus::NoPath;
  agent.phase = PathAgentPhase::NeedsPath;
  agent.blocked_retries = 0;
  agent.has_goal = true;
}

/// Returns `agent` to the idle state and invalidates its route ticket.
inline void clear_path_agent_goal(PathAgentState& agent) noexcept {
  agent.goal = {};
  agent.ticket = {};
  agent.path_index = 0;
  agent.status = PathStatus::NoPath;
  agent.phase = PathAgentPhase::Idle;
  agent.blocked_retries = 0;
  agent.has_goal = false;
}

/// Rebuilds runtime requests according to `scope` and returns submit counts.
inline auto submit_path_agents(std::span<PathAgentState> agents,
                               PathRequestRuntime& runtime,
                               PathSubmitScope scope = PathSubmitScope::All)
    -> PathAgentFrameStats {
  PathAgentFrameStats stats;
  runtime.clear_requests();

  for (auto& agent : agents) {
    if (scope == PathSubmitScope::NeedsOnly &&
        detail::can_skip_scoped_path_submission(agent)) {
      // Keeps its retained route and path_index; the runtime rebuild below
      // makes its old ticket stale, which nothing reads in the scoped flow.
      continue;
    }
    agent.path_index = 0;
    if (!agent.has_goal || agent.phase == PathAgentPhase::Unreachable) {
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

/// Increments the result counter corresponding to `status`.
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
    case PathStatus::Indeterminate:
      ++stats.indeterminate;
      return;
    case PathStatus::CostOverflow:
      ++stats.cost_overflow;
      return;
  }
}

/// Applies runtime results to matching agent state and optional retained
/// routes.
inline auto apply_path_agent_results(std::span<PathAgentState> agents,
                                     const PathRequestRuntime& runtime,
                                     PathSubmitScope scope,
                                     PathAgentRoutes* routes)
    -> PathAgentFrameStats {
  PathAgentFrameStats stats;
  if (routes != nullptr) {
    // Callers going through the tick drivers arrive pre-sized; grow here
    // too so the public process_* overloads cannot index out of bounds.
    routes->ensure_size(agents.size());
  }

  for (std::size_t i = 0; i < agents.size(); ++i) {
    auto& agent = agents[i];
    if (scope == PathSubmitScope::NeedsOnly &&
        detail::can_skip_scoped_path_submission(agent)) {
      // Not resubmitted by the matching scoped submit; its runtime ticket
      // is stale and its retained route stays as-is.
      continue;
    }
    if (!agent.has_goal || agent.position == agent.goal ||
        agent.phase == PathAgentPhase::Unreachable) {
      continue;
    }

    const auto was_blocked = agent.phase == PathAgentPhase::Blocked;
    const auto result = runtime.result(agent.ticket);
    agent.status = result.status;
    agent.path_index = 0;
    if (result.status == PathStatus::Found) {
      agent.phase = PathAgentPhase::Following;
      // A Found search is not progress for an occupancy-blocked agent: the
      // planner intentionally ignores occupancy and may return the identical
      // next step. Preserve its consecutive-block budget until movement
      // actually succeeds.
      if (!was_blocked) {
        agent.blocked_retries = 0;
      }
      if (routes != nullptr) {
        routes->routes[i].assign(result.path.begin(), result.path.end());
      }
    } else {
      // Planner failures are retried through the Blocked lifecycle until
      // the tick driver's retry budget runs out.
      agent.phase = PathAgentPhase::Blocked;
      if (routes != nullptr) {
        routes->routes[i].clear();
      }
    }
    ++stats.completed;
    record_path_agent_status(stats, result.status);
  }

  return stats;
}

/// Applies completed runtime results to every agent in the span.
inline auto apply_path_agent_results(std::span<PathAgentState> agents,
                                     const PathRequestRuntime& runtime)
    -> PathAgentFrameStats {
  return apply_path_agent_results(agents, runtime, PathSubmitScope::All,
                                  nullptr);
}

/// Advances agents along runtime-owned paths without validating world movement.
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

// Observer form: `on_commit(agent_index, from, to)` is invoked once per
// successful commit_movement_intent, after the agent's position and the
// world's occupancy fields are updated and before arrival handling. It is
// never invoked for a failed validation (nothing was written to the world),
// so external tile->entity mirrors that update inside the callback stay
// synchronized with the occupancy field by construction.
template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename OnCommit>
  requires std::invocable<OnCommit&, std::size_t, Coord3, Coord3>
/// Advances runtime paths through validated world movement commits.
inline auto advance_path_agents_with_movement(World& world,
                                              std::span<PathAgentState> agents,
                                              const PathRequestRuntime& runtime,
                                              std::size_t max_steps,
                                              std::uint32_t movement_dirty_mask,
                                              OnCommit&& on_commit)
    -> PathAgentFrameStats {
  PathAgentFrameStats stats;
  if (max_steps == 0) {
    return stats;
  }

  for (std::size_t agent_index = 0; agent_index < agents.size();
       ++agent_index) {
    auto& agent = agents[agent_index];
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
          commit_movement_intent<World, ClassOrTag, OccupancyTag,
                                 ReservationTag>(
              world, MovementIntent{from, to, {}}, movement_dirty_mask);
      if (movement.status != MovementStatus::Moved) {
        record_movement_failure(stats.movement_failures, movement.status);
        if (is_transient_movement_failure(movement.status)) {
          // Wait in place. Occupancy/reservation failures retain Found so the
          // same step can be retried without a pointless occupancy-blind
          // search. Other transient failures set NoPath and request a fresh
          // route. The following tick starts consuming the shared bounded
          // retry budget (see PathAgentTickOptions::max_blocked_retries).
          detail::block_path_agent(agent, movement.status);
          ++stats.blocked_waits;
        } else {
          // Invalid endpoints or a non-adjacent step indicate a caller
          // bug; terminal until a new goal re-arms the lifecycle.
          agent.status = PathStatus::NoPath;
          agent.phase = PathAgentPhase::Unreachable;
        }
        break;
      }

      ++agent.path_index;
      agent.position = to;
      detail::resume_path_agent(agent);
      on_commit(agent_index, from, to);
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

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
/// Advances runtime-routed agents with validated world movement.
inline auto advance_path_agents_with_movement(
    World& world, std::span<PathAgentState> agents,
    const PathRequestRuntime& runtime, std::size_t max_steps = 1,
    std::uint32_t movement_dirty_mask = 0) -> PathAgentFrameStats {
  return advance_path_agents_with_movement<World, ClassOrTag, OccupancyTag,
                                           ReservationTag>(
      world, agents, runtime, max_steps, movement_dirty_mask,
      [](std::size_t, Coord3, Coord3) {});
}

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename Provider>
/// Advances retained routes through provider-aware movement commits.
inline auto advance_path_agents_with_movement(World& world,
                                              std::span<PathAgentState> agents,
                                              const PathAgentRoutes& routes,
                                              std::size_t max_steps,
                                              std::uint32_t movement_dirty_mask,
                                              const Provider& provider)
    -> PathAgentFrameStats {
  TESS_ASSERT(routes.routes.size() >= agents.size());
  PathAgentFrameStats stats;
  if (max_steps == 0) {
    return stats;
  }
  for (std::size_t agent_index = 0; agent_index < agents.size();
       ++agent_index) {
    auto& agent = agents[agent_index];
    if (!agent.has_goal || agent.status != PathStatus::Found) {
      continue;
    }
    const auto& route = routes.routes[agent_index];
    for (std::size_t step = 0;
         step < max_steps && agent.path_index + 1 < route.size(); ++step) {
      const auto from = agent.position;
      const auto to = route[agent.path_index + 1];
      const auto movement =
          commit_movement_intent<World, ClassOrTag, OccupancyTag,
                                 ReservationTag>(world,
                                                 MovementIntent{from, to, {}},
                                                 movement_dirty_mask, provider);
      if (movement.status != MovementStatus::Moved) {
        record_movement_failure(stats.movement_failures, movement.status);
        if (is_transient_movement_failure(movement.status)) {
          detail::block_path_agent(agent, movement.status);
          ++stats.blocked_waits;
        } else {
          agent.status = PathStatus::NoPath;
          agent.phase = PathAgentPhase::Unreachable;
        }
        break;
      }
      ++agent.path_index;
      agent.position = to;
      detail::resume_path_agent(agent);
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

// Route-pool advance: identical stepping semantics to the runtime-reading
// overloads above, but the route comes from the retained pool, so it
// survives processing passes that did not resubmit this agent
// (PathSubmitScope::NeedsOnly).
/// Advances agents along retained routes without validating world movement.
inline auto advance_path_agents(std::span<PathAgentState> agents,
                                const PathAgentRoutes& routes,
                                std::size_t max_steps = 1)
    -> PathAgentFrameStats {
  // The pool is const here, so it must already cover the span (the tick
  // drivers ensure_size before processing; direct callers must too).
  TESS_ASSERT(routes.routes.size() >= agents.size());
  PathAgentFrameStats stats;
  if (max_steps == 0) {
    return stats;
  }

  for (std::size_t i = 0; i < agents.size(); ++i) {
    auto& agent = agents[i];
    if (!agent.has_goal || agent.status != PathStatus::Found) {
      continue;
    }

    const auto& route = routes.routes[i];
    if (route.empty()) {
      continue;
    }

    for (std::size_t step = 0; step < max_steps; ++step) {
      if (agent.path_index + 1 >= route.size()) {
        break;
      }
      ++agent.path_index;
      agent.position = route[agent.path_index];
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

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag, typename OnCommit>
  requires std::invocable<OnCommit&, std::size_t, Coord3, Coord3>
/// Advances retained routes and reports each validated movement commit.
inline auto advance_path_agents_with_movement(World& world,
                                              std::span<PathAgentState> agents,
                                              const PathAgentRoutes& routes,
                                              std::size_t max_steps,
                                              std::uint32_t movement_dirty_mask,
                                              OnCommit&& on_commit)
    -> PathAgentFrameStats {
  // Same pool-coverage precondition as the plain route-pool advance.
  TESS_ASSERT(routes.routes.size() >= agents.size());
  PathAgentFrameStats stats;
  if (max_steps == 0) {
    return stats;
  }

  for (std::size_t agent_index = 0; agent_index < agents.size();
       ++agent_index) {
    auto& agent = agents[agent_index];
    if (!agent.has_goal || agent.status != PathStatus::Found) {
      continue;
    }

    const auto& route = routes.routes[agent_index];
    if (route.empty()) {
      continue;
    }

    for (std::size_t step = 0; step < max_steps; ++step) {
      if (agent.path_index + 1 >= route.size()) {
        break;
      }

      const auto from = agent.position;
      const auto to = route[agent.path_index + 1];
      const auto movement =
          commit_movement_intent<World, ClassOrTag, OccupancyTag,
                                 ReservationTag>(
              world, MovementIntent{from, to, {}}, movement_dirty_mask);
      if (movement.status != MovementStatus::Moved) {
        record_movement_failure(stats.movement_failures, movement.status);
        if (is_transient_movement_failure(movement.status)) {
          // Same Blocked/Unreachable split as the runtime-reading overload;
          // see its comment for the retry-budget semantics.
          detail::block_path_agent(agent, movement.status);
          ++stats.blocked_waits;
        } else {
          agent.status = PathStatus::NoPath;
          agent.phase = PathAgentPhase::Unreachable;
        }
        break;
      }

      ++agent.path_index;
      agent.position = to;
      detail::resume_path_agent(agent);
      on_commit(agent_index, from, to);
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

template <typename World, typename ClassOrTag, typename OccupancyTag,
          typename ReservationTag>
/// Advances retained routes with validated world movement.
inline auto advance_path_agents_with_movement(
    World& world, std::span<PathAgentState> agents,
    const PathAgentRoutes& routes, std::size_t max_steps = 1,
    std::uint32_t movement_dirty_mask = 0) -> PathAgentFrameStats {
  return advance_path_agents_with_movement<World, ClassOrTag, OccupancyTag,
                                           ReservationTag>(
      world, agents, routes, max_steps, movement_dirty_mask,
      [](std::size_t, Coord3, Coord3) {});
}

/// Adds every counter in `rhs` into `lhs`.
inline void add_path_agent_stats(PathAgentFrameStats& lhs,
                                 PathAgentFrameStats rhs) noexcept {
  lhs.submitted += rhs.submitted;
  lhs.completed += rhs.completed;
  lhs.found += rhs.found;
  lhs.invalid_start += rhs.invalid_start;
  lhs.invalid_goal += rhs.invalid_goal;
  lhs.no_path += rhs.no_path;
  lhs.indeterminate += rhs.indeterminate;
  lhs.cost_overflow += rhs.cost_overflow;
  lhs.precheck_ruled_out += rhs.precheck_ruled_out;
  lhs.advanced += rhs.advanced;
  lhs.arrived += rhs.arrived;
  lhs.blocked_waits += rhs.blocked_waits;
  lhs.movement_failures.invalid += rhs.movement_failures.invalid;
  lhs.movement_failures.blocked += rhs.movement_failures.blocked;
  lhs.movement_failures.occupied += rhs.movement_failures.occupied;
  lhs.movement_failures.reserved += rhs.movement_failures.reserved;
  lhs.movement_failures.stale_version += rhs.movement_failures.stale_version;
  lhs.movement_failures.stale_topology += rhs.movement_failures.stale_topology;
}

template <typename World, typename ClassOrTag>
/// Submits, solves, and applies unit-cost paths for a batch of agents.
[[nodiscard]] auto process_unit_path_agents(
    const World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathRuntimeCachePolicy policy = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    PathSubmitScope scope = PathSubmitScope::All,
    PathAgentRoutes* routes = nullptr) -> PathAgentFrameStats {
  auto stats = submit_path_agents(agents, runtime, scope);
  (void)runtime.template process_unit_cached<World, ClassOrTag>(world, policy,
                                                                graph);
  add_path_agent_stats(
      stats, apply_path_agent_results(agents, runtime, scope, routes));
  stats.precheck_ruled_out = runtime.stats().precheck_ruled_out;
  return stats;
}

template <typename World, typename ClassOrTag, typename Provider>
/// Solves unit-cost agent routes composed with a special provider.
[[nodiscard]] auto process_unit_path_agents(
    const World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathRuntimeCachePolicy policy,
    const RegionGraphT<typename World::residency_type>* graph,
    PathSubmitScope scope, PathAgentRoutes* routes, const Provider& provider)
    -> PathAgentFrameStats {
  auto stats = submit_path_agents(agents, runtime, scope);
  (void)runtime.template process_unit_cached<World, ClassOrTag>(
      world, policy, graph, provider);
  add_path_agent_stats(
      stats, apply_path_agent_results(agents, runtime, scope, routes));
  stats.precheck_ruled_out = runtime.stats().precheck_ruled_out;
  return stats;
}

template <typename World, typename Class, std::uint32_t MaxCost>
/// Submits, solves, and applies bounded weighted paths for a batch of agents.
[[nodiscard]] auto process_weighted_path_agents(
    const World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathRuntimeCachePolicy policy = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    PathSubmitScope scope = PathSubmitScope::All,
    PathAgentRoutes* routes = nullptr) -> PathAgentFrameStats {
  auto stats = submit_path_agents(agents, runtime, scope);
  (void)runtime.template process_weighted_batch<World, Class, MaxCost>(
      world, policy, graph);
  add_path_agent_stats(
      stats, apply_path_agent_results(agents, runtime, scope, routes));
  stats.precheck_ruled_out = runtime.stats().precheck_ruled_out;
  return stats;
}

template <typename World, typename Class, std::uint32_t MaxCost,
          typename Provider>
/// Solves weighted agent routes composed with a special provider.
[[nodiscard]] auto process_weighted_path_agents(
    const World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathRuntimeCachePolicy policy,
    const RegionGraphT<typename World::residency_type>* graph,
    PathSubmitScope scope, PathAgentRoutes* routes, const Provider& provider)
    -> PathAgentFrameStats {
  auto stats = submit_path_agents(agents, runtime, scope);
  (void)runtime.template process_weighted_batch<World, Class, MaxCost>(
      world, policy, graph, provider);
  add_path_agent_stats(
      stats, apply_path_agent_results(agents, runtime, scope, routes));
  stats.precheck_ruled_out = runtime.stats().precheck_ruled_out;
  return stats;
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
/// Processes bounded weighted paths using separate legacy field tags.
[[nodiscard]] auto process_weighted_path_agents(
    const World& world, std::span<PathAgentState> agents,
    PathRequestRuntime& runtime, PathRuntimeCachePolicy policy = {},
    const RegionGraphT<typename World::residency_type>* graph = nullptr,
    PathSubmitScope scope = PathSubmitScope::All,
    PathAgentRoutes* routes = nullptr) -> PathAgentFrameStats {
  auto stats = submit_path_agents(agents, runtime, scope);
  (void)runtime
      .template process_weighted_batch<World, PassableTag, CostTag, MaxCost>(
          world, policy, graph);
  add_path_agent_stats(
      stats, apply_path_agent_results(agents, runtime, scope, routes));
  stats.precheck_ruled_out = runtime.stats().precheck_ruled_out;
  return stats;
}

}  // namespace tess

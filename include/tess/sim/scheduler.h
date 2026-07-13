#pragma once

#include <tess/ops/queued.h>
#include <tess/sim/path_agent_tick.h>
#include <tess/sim/render_delta.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace tess {

/// Owns simulation subsystem state retained across scheduler ticks.
struct SimSchedulerState {
  PathAgentTickState path_agents{};
};

/// Configures dirty propagation, path-agent work, and render-delta clearing.
struct SimSchedulerOptions {
  std::uint32_t pathing_dirty_mask = 0;
  std::uint32_t render_dirty_mask = 0;
  PathAgentTickOptions path_agent_options{};
  bool clear_render_dirty = false;
  std::uint32_t movement_dirty_mask = 0;
};

/// Summarizes queued operations, path agents, and render deltas for one tick.
struct SimSchedulerStats {
  std::uint64_t tick = 0;
  bool planned_ops = false;
  bool executed_ops = false;
  ExecutionReport op_report{};
  PlannedExecutionResult op_execution{};
  PathAgentTickStats path_agents{};
  std::size_t render_delta_count = 0;
};

/// Plans and executes one immutable queued-operation frame against `world`.
template <typename World, WritePolicy Policy, typename Fn>
auto run_queued_operations(World& world, const FrameOps& ops, Fn&& fn)
    -> SimSchedulerStats {
  SimSchedulerStats stats;
  stats.op_report = plan_operations(world, ops);
  stats.planned_ops = !ops.empty();
  if (!stats.op_report.ok()) {
    return stats;
  }
  stats.op_execution = execute_plan<Policy>(world, stats.op_report.plan(), fn);
  stats.executed_ops =
      stats.op_execution.status == PlannedExecutionStatus::Executed;
  return stats;
}

namespace detail {

// Shared tick sequence for all scheduler variants: run queued operations,
// mark pathing dirty when planned work dirtied configured pathing fields,
// run the variant-specific path-agent tick, then collect (and optionally
// clear) render deltas. `path_tick` returns the variant's
// `PathAgentTickStats`.
template <typename World, WritePolicy Policy, typename Fn, typename PathTick>
auto tick_scheduler_core(SimSchedulerState& state, World& world,
                         const FrameOps& ops,
                         std::vector<RenderTileDelta>& render_deltas, Fn&& fn,
                         const SimSchedulerOptions& options,
                         PathTick&& path_tick) -> SimSchedulerStats {
  auto stats =
      run_queued_operations<World, Policy>(world, ops, std::forward<Fn>(fn));
  // A plan can abort partway (e.g. PolicyMismatch): operations that
  // already executed have applied their world writes, and execute_plan
  // reports their chunks in the aborted result's chunk_count. Gate on any
  // executed chunk rather than on full-plan success so path caches are
  // refreshed over partially applied plans too.
  const bool any_op_wrote =
      stats.executed_ops || stats.op_execution.chunk_count > 0;
  if (any_op_wrote && options.pathing_dirty_mask != 0) {
    for (const auto& report : stats.op_report.operations()) {
      if (report.status == OperationStatus::Planned &&
          (report.field_access.dirty_mask & options.pathing_dirty_mask) != 0) {
        mark_pathing_dirty(state.path_agents);
        break;
      }
    }
  }

  stats.path_agents = path_tick();
  stats.tick = stats.path_agents.tick;

  const auto old_size = render_deltas.size();
  collect_render_tile_deltas(world, options.render_dirty_mask, render_deltas);
  stats.render_delta_count = render_deltas.size() - old_size;
  if (options.clear_render_dirty) {
    clear_render_delta_dirty(world, options.render_dirty_mask);
  }
  return stats;
}

}  // namespace detail

/// Runs queued work, unit-cost path agents, and render-delta collection.
template <typename World, typename PassableTag, WritePolicy Policy, typename Fn>
auto tick_unit_scheduler(SimSchedulerState& state, World& world,
                         const FrameOps& ops, std::span<PathAgentState> agents,
                         PathRequestRuntime& runtime,
                         std::vector<RenderTileDelta>& render_deltas, Fn&& fn,
                         SimSchedulerOptions options = {})
    -> SimSchedulerStats {
  return detail::tick_scheduler_core<World, Policy>(
      state, world, ops, render_deltas, std::forward<Fn>(fn), options, [&] {
        return tick_unit_path_agents<World, PassableTag>(
            state.path_agents, world, agents, runtime,
            options.path_agent_options);
      });
}

template <typename World, typename PassableTag, typename OccupancyTag,
          typename ReservationTag, WritePolicy Policy, typename Fn>
/// Runs a unit-cost tick whose agent steps commit occupancy changes.
auto tick_unit_movement_scheduler(SimSchedulerState& state, World& world,
                                  const FrameOps& ops,
                                  std::span<PathAgentState> agents,
                                  PathRequestRuntime& runtime,
                                  std::vector<RenderTileDelta>& render_deltas,
                                  Fn&& fn, SimSchedulerOptions options = {})
    -> SimSchedulerStats {
  return detail::tick_scheduler_core<World, Policy>(
      state, world, ops, render_deltas, std::forward<Fn>(fn), options, [&] {
        return tick_unit_path_agents_with_movement<
            World, PassableTag, OccupancyTag, ReservationTag>(
            state.path_agents, world, agents, runtime,
            options.path_agent_options, options.movement_dirty_mask);
      });
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost, WritePolicy Policy, typename Fn>
/// Runs queued work, bounded weighted agents, and render-delta collection.
auto tick_weighted_scheduler(SimSchedulerState& state, World& world,
                             const FrameOps& ops,
                             std::span<PathAgentState> agents,
                             PathRequestRuntime& runtime,
                             std::vector<RenderTileDelta>& render_deltas,
                             Fn&& fn, SimSchedulerOptions options = {})
    -> SimSchedulerStats {
  return detail::tick_scheduler_core<World, Policy>(
      state, world, ops, render_deltas, std::forward<Fn>(fn), options, [&] {
        return tick_weighted_path_agents<World, PassableTag, CostTag, MaxCost>(
            state.path_agents, world, agents, runtime,
            options.path_agent_options);
      });
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost, typename OccupancyTag, typename ReservationTag,
          WritePolicy Policy, typename Fn>
/// Runs a bounded weighted tick whose agent steps commit occupancy changes.
auto tick_weighted_movement_scheduler(
    SimSchedulerState& state, World& world, const FrameOps& ops,
    std::span<PathAgentState> agents, PathRequestRuntime& runtime,
    std::vector<RenderTileDelta>& render_deltas, Fn&& fn,
    SimSchedulerOptions options = {}) -> SimSchedulerStats {
  return detail::tick_scheduler_core<World, Policy>(
      state, world, ops, render_deltas, std::forward<Fn>(fn), options, [&] {
        return tick_weighted_path_agents_with_movement<
            World, PassableTag, CostTag, MaxCost, OccupancyTag, ReservationTag>(
            state.path_agents, world, agents, runtime,
            options.path_agent_options, options.movement_dirty_mask);
      });
}

}  // namespace tess

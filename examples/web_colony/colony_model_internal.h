#pragma once

#include <tess/core/config.h>
#include <tess/pathfinding.h>
#include <tess/simulation.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "colony_model.h"

namespace tess::examples::web_colony {

// Tutorial map:
//   1. Define terrain and movement classes.
//   2. Queue edits into a deterministic schedule.
//   3. Rebuild topology only when terrain becomes dirty.
//   4. Plan a bounded amount of work, then commit movement.
//   5. Publish invalidation frames and keep presentation state separate.

struct PassableTag {};
struct ConstructionTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};
// Set on the tile of every agent quiescent for the current leg -- one that has
// arrived, is crowd-blocked until turnaround, or is durably unreachable.
// Distinct from occupancy, which a travelling agent also sets and vacates a
// tick later.
struct SettledTag {};

constexpr int kWidth = width;
constexpr int kHeight = height;
constexpr int kMaxAgents = max_agents;
// Eight mostly populated endpoint columns alternate with permanent access
// aisles. One shared row remains open through every such column, so settled
// colonists cannot turn endpoint parking into a full-height wall. Wall
// painting stays outside the complete endpoint bands.
constexpr int kEndpointBandWidth = 18;
constexpr int kWallMinX = kEndpointBandWidth;
constexpr int kWallMaxX = kWidth - kEndpointBandWidth - 1;
constexpr int kApproachGuardColumns = 8;
constexpr std::size_t kApproachBarrierTiles = kHeight / 2;
// Recovery probes begin after half this window. It only has to be long enough
// that ordinary convoy shuffling does not pay for an exact reachability probe.
constexpr std::uint32_t kRecoveryWindowTicks = 32;
constexpr std::size_t kMaxPlanningQueriesPerTick = 8;
// Pointer painting can publish one topology revision per fixed tick. Delay a
// congestion response until the stroke has been quiet long enough that its
// seeded snapshot describes the visible wall rather than a partial one.
constexpr std::uint64_t kTopologyIdleTicks = 8;
constexpr auto kRecoveryOptions = tess::BlockedAgentRecoveryOptions{
    .initial_delay_ticks = kRecoveryWindowTicks / 2,
    .max_delay_ticks = 256,
    .max_probes_per_tick = 8,
    .jitter_seed = 0x434f4c4f4e59ULL,
};

using Shape =
    tess::Shape<tess::Extent3{kWidth, kHeight, 1}, tess::Extent3{16, 16, 1}>;
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<ConstructionTag, bool>,
    tess::Field<CostTag, std::uint32_t>, tess::Field<OccupancyTag, bool>,
    tess::Field<ReservationTag, bool>, tess::Field<SettledTag, bool>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

using Walker = tess::movement::MovementClass<
    tess::movement::AllOf<
        tess::movement::Field<PassableTag>,
        tess::movement::Not<tess::movement::Field<ConstructionTag>>>,
    tess::movement::FieldCost<CostTag>>;
// What the agents themselves plan and move with: the terrain rules plus "no
// settled colonist is standing there". Occupancy at large stays out of
// planning on purpose -- travelling peers move on, so routing around them
// would thrash routes for nothing -- but a settled agent does not move again
// during this leg, which makes its tile as solid as a wall until turnaround.
// Leaving it out of planning deadlocks any agent whose goal lies beyond one (a
// bottleneck makes that routine: see the native verification scenarios).
using Traveler = tess::movement::MovementClass<
    tess::movement::AllOf<
        tess::movement::Field<PassableTag>,
        tess::movement::Not<tess::movement::Field<ConstructionTag>>,
        tess::movement::Not<tess::movement::Field<SettledTag>>>,
    tess::movement::FieldCost<CostTag>>;
constexpr std::uint32_t kMaxCost = 1;
constexpr std::uint32_t kTerrainDirty = 1U << 0U;

struct BuildAck {
  std::size_t tiles = 0;
};

// Convoy layout: batch k = i / kHeight walks row y = i % kHeight between
// columns 16 - 2k (home) and 125 - 2k (away). Its middle-row agent parks in a
// sparse outer column instead. That leaves row 64 as a cross-cut through every
// otherwise populated column while preserving distinct, equal-length goals.
constexpr auto home_tile(std::size_t i) -> tess::Coord3 {
  const auto batch = static_cast<std::int64_t>(i / kHeight);
  if (i % kHeight == kHeight / 2) {
    return {17, 56 + batch, 0};
  }
  return {16 - 2 * batch, static_cast<std::int64_t>(i % kHeight), 0};
}
constexpr auto away_tile(std::size_t i) -> tess::Coord3 {
  const auto batch = static_cast<std::int64_t>(i / kHeight);
  if (i % kHeight == kHeight / 2) {
    return {kWidth - 2, 56 + batch, 0};
  }
  return {kWidth - 3 - 2 * batch, static_cast<std::int64_t>(i % kHeight), 0};
}

struct ColonyModel::Impl {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathRequestRuntime runtime;
  tess::PathAgentTickState tick_state;
  tess::BlockedAgentRecoverySchedule recovery_schedule;
  tess::PathAgentReplanQueue replan_queue;
  tess::PathAgentReplanQueue diversity_replan_queue;
  tess::PathAgentReplanQueue wide_merge_replan_queue;
  tess::LocalTopologyScratch topo_scratch;
  tess::PathScratch settle_scratch;
  tess::PathScratch replan_scratch;
  tess::JointMoveScratch joint_scratch;
  tess::RegionGraphScratch graph_scratch;
  tess::RegionGraph graph;
  tess::FrameOps ops;
  tess::DeltaCollector deltas;
  std::vector<tess::Coord3> pending_walls;
  std::vector<std::uint8_t> merge_claims;
  std::vector<tess::ChunkKey> dirty_scratch;
  std::vector<std::uint8_t> shadow;  // 0 open, 1 wall, per tile.
  // Presentation snapshots. Logical positions remain the integer coordinates
  // in agents; JavaScript interpolates these copies and never writes back.
  std::vector<std::int16_t> previous_agent_xy;
  std::vector<std::int16_t> agent_xy;
  double interpolation_alpha = 0.0;
  tess::RenderVersion version{};
  std::size_t built_tiles = 0;
  bool replan_each_tick = false;
  bool spread_congested_routes = false;
  bool diversity_wave_attempted = false;
  bool routes_diversified = false;
  bool wide_merge_checked = false;
  std::size_t left_approach_wall_tiles = 0;
  std::size_t right_approach_wall_tiles = 0;
  std::size_t diversity_replan_waves = 0;
  std::size_t wide_merge_replan_waves = 0;
  std::size_t wide_merge_tiles = 0;
  std::uint64_t last_topology_edit_tick = 0;
  // Persistent schedule clock and accumulator: the carry between frames is
  // what turns measured real deltas into a wall-clock 20 Hz simulation.
  // (tick_state owns the separate agent-tick clock; leave it alone.)
  tess::SimClock sim_clock;
  tess::FixedStepAccumulator accumulator{20, 8};
  std::vector<std::uint8_t> crowd_blocked;
  std::vector<std::uint8_t> terrain_confirmation_pending;
  bool outbound = true;
  int leg = 1;
  int completed_legs = 0;
  int aborted_legs = 0;
  // Consecutive fixed ticks with zero agent movement. See AgentTaskFn.
  std::size_t stalled_ticks = 0;
  std::size_t last_advanced = 0;
  std::size_t last_movement_waits = 0;
  std::size_t max_recovery_probes = 0;
  std::size_t max_planning_queries = 0;

  struct BuildTaskFn {
    Impl* demo;
    template <typename View>
    void operator()(View& view, BuildAck& ack) const {
      auto passable = view.template field_span<PassableTag>();
      auto construction = view.template field_span<ConstructionTag>();
      for (const auto& coord : demo->pending_walls) {
        const auto key =
            tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord));
        if (key != view.key()) {
          continue;
        }
        const auto local =
            tess::local_tile_id<Shape>(tess::local_coord<Shape>(coord));
        const auto newly_constructed = !construction[local.value];
        passable[local.value] = false;
        construction[local.value] = true;
        if (newly_constructed && coord.x < kWallMinX + kApproachGuardColumns) {
          ++demo->left_approach_wall_tiles;
        }
        if (newly_constructed && coord.x > kWallMaxX - kApproachGuardColumns) {
          ++demo->right_approach_wall_tiles;
        }
        ++ack.tiles;
      }
    }
  };

  using BuildTask = tess::AutoExecTask<World, tess::WritePolicy::UniquePerChunk,
                                       BuildAck, BuildTaskFn>;
  std::unique_ptr<BuildTask> build_task;

  void snapshot_before_movement() { previous_agent_xy = agent_xy; }

  void snapshot_after_movement() {
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agent_xy[i * 2] = static_cast<std::int16_t>(agents[i].position.x);
      agent_xy[i * 2 + 1] = static_cast<std::int16_t>(agents[i].position.y);
    }
  }

  explicit Impl(int agent_count);
  void initialize_world();
  void reserve_working_memory(std::size_t agent_count);
  void initialize_agents(std::size_t agent_count);
  void configure_build_task();
  void configure_schedule();
  void initialize_render_consumer();

  struct TopologyTaskFn {
    Impl* demo = nullptr;
    auto operator()(const tess::ScheduleTaskContext& context)
        -> tess::ScheduleTaskResult {
      // Example: rebuild derived topology on dirty input. The schedule runs
      // this OnDirty task only after queued terrain edits publish their bit.
      demo->dirty_scratch.clear();
      demo->world.collect_dirty_chunks(kTerrainDirty, demo->dirty_scratch);
      if (!demo->dirty_scratch.empty()) {
        const auto updated = tess::update_region_graph<World, Walker>(
            demo->world, demo->topo_scratch, demo->graph, demo->dirty_scratch);
        // collect_dirty_chunks returns keys owned by this dense world, so an
        // invalid chunk would violate the model's internal pairing invariant.
        if (updated.status != tess::TopologyStatus::Built) {
          TESS_ASSERT(false);
          return {};
        }
        // A topology revision ends any older seeded snapshot. Canonical work
        // owns the new revision immediately; a fresh seed becomes eligible
        // only after pointer painting has remained idle for a short window.
        demo->diversity_replan_queue.clear();
        demo->wide_merge_replan_queue.clear();
        demo->replan_queue.request_all(demo->agents);
        demo->routes_diversified = false;
        demo->diversity_wave_attempted = false;
        demo->wide_merge_checked = false;
        demo->last_topology_edit_tick = context.clock.tick;
      }
      return {};
    }
  };

  // Example: publish transient passability before planning. An agent that is
  // quiescent for this leg is an obstacle until turnaround, so `Traveler`
  // routes around it. Occupancy cannot express this: travelling agents also
  // occupy tiles but move on during the same leg.
  void sync_settled_obstacles() {
    for (auto& agent : agents) {
      const auto settled =
          !agent.has_goal || agent.phase == tess::PathAgentPhase::Unreachable;
      if ((world.field<SettledTag>(agent.position) != 0) == settled) {
        continue;
      }
      world.field<SettledTag>(agent.position) = settled;
      // Settling is a world edit as far as the unit route cache is concerned,
      // because `Traveler` reads this field: the tile just changed
      // passability. That cache invalidates on chunk versions, and a plain
      // field write bumps none, so without this the next replan can be handed
      // a cached route straight through the tile that has just become
      // impassable -- and the agent then retries that step forever, kept alive
      // but never unblocked by the retry refund below.
      //
      // This is content invalidation, not terrain scheduling: bump the chunk
      // version without setting a dirty bit that would wake topology or render
      // consumers. Only notify on an actual value change, or every fixed tick
      // would invalidate caches needlessly.
      const auto key =
          tess::chunk_key<Shape>(tess::chunk_coord<Shape>(agent.position));
      world.mark_content_changed(key);
    }
  }

  [[nodiscard]] auto endpoint_approach_obstructed() const noexcept -> bool {
    // A few wall endpoints can cross this band without causing the one-sided
    // endpoint convergence this guard protects. Require a substantial local
    // barrier so unrelated interior congestion can still use route spreading.
    return left_approach_wall_tiles >= kApproachBarrierTiles ||
           right_approach_wall_tiles >= kApproachBarrierTiles;
  }

  [[nodiscard]] auto dominant_barrier_open_runs() const -> std::size_t {
    auto barrier_x = kWallMinX;
    auto barrier_tiles = 0;
    auto barrier_straddlers = 0;
    const auto active = static_cast<std::size_t>(outstanding_goal_count());
    const auto minimum_straddlers = std::max(std::size_t{8}, active / 4U);
    for (auto x = kWallMinX; x <= kWallMaxX; ++x) {
      auto construction_tiles = 0;
      for (auto y = 0; y < kHeight; ++y) {
        construction_tiles +=
            world.field<ConstructionTag>(tess::Coord3{x, y, 0}) ? 1 : 0;
      }
      auto straddlers = 0;
      for (const auto& agent : agents) {
        if (!agent.has_goal) {
          continue;
        }
        const auto crosses = outbound
                                 ? agent.position.x < x && agent.goal.x >= x
                                 : agent.position.x > x && agent.goal.x <= x;
        straddlers += crosses ? 1 : 0;
      }
      if (static_cast<std::size_t>(straddlers) < minimum_straddlers) {
        continue;
      }
      if (construction_tiles > barrier_tiles ||
          (construction_tiles == barrier_tiles &&
           straddlers > barrier_straddlers)) {
        barrier_tiles = construction_tiles;
        barrier_straddlers = straddlers;
        barrier_x = x;
      }
    }
    if (barrier_tiles < kHeight / 2) {
      return 0;
    }

    auto runs = std::size_t{0};
    auto in_run = false;
    for (auto y = 0; y < kHeight; ++y) {
      const auto open =
          !world.field<ConstructionTag>(tess::Coord3{barrier_x, y, 0});
      if (open && !in_run) {
        ++runs;
      }
      in_run = open;
    }
    return runs;
  }

  void update_route_diversity(std::uint64_t tick) {
    if (!spread_congested_routes || replan_each_tick ||
        endpoint_approach_obstructed()) {
      if (!diversity_replan_queue.empty() || routes_diversified) {
        diversity_replan_queue.clear();
        routes_diversified = false;
        replan_queue.request_all(agents);
      }
      return;
    }
    const auto topology_idle =
        tick >= last_topology_edit_tick &&
        tick - last_topology_edit_tick >= kTopologyIdleTicks;
    if (diversity_wave_attempted || !topology_idle) {
      return;
    }
    const auto active = static_cast<std::size_t>(outstanding_goal_count());
    const auto threshold = std::max(std::size_t{8}, active / 16);
    if (active < 64 || last_movement_waits < threshold) {
      return;
    }
    // A broad barrier with several separate openings already supplies route
    // diversity. Seeding those routes can synchronize agents that canonical
    // planning had naturally split, so observe this topology once and leave
    // the retained routes alone for the rest of the leg.
    if (dominant_barrier_open_runs() > 2) {
      diversity_wave_attempted = true;
      return;
    }
    diversity_wave_attempted = true;
    routes_diversified = true;
    ++diversity_replan_waves;
    // The seeded policy owns exactly this snapshot. Later topology and
    // recovery requests remain in the canonical queue and cannot inherit it.
    replan_queue.clear();
    diversity_replan_queue.request_all(agents);
  }

  [[nodiscard]] auto count_wide_merge_tiles() -> std::size_t {
    std::fill(merge_claims.begin(), merge_claims.end(), std::uint8_t{0});
    for (std::size_t i = 0; i < agents.size(); ++i) {
      const auto& agent = agents[i];
      if (!agent.has_goal || agent.status != tess::PathStatus::Found ||
          agent.phase != tess::PathAgentPhase::Blocked ||
          i >= tick_state.routes.routes.size()) {
        continue;
      }
      const auto& route = tick_state.routes.routes[i];
      if (agent.path_index + 1 >= route.size()) {
        continue;
      }
      const auto desired = route[agent.path_index + 1];
      const auto key = tess::tile_key<Shape>(desired);
      auto& claims = merge_claims[static_cast<std::size_t>(key.value)];
      claims = std::min<std::uint8_t>(claims + 1, 2);
    }
    return static_cast<std::size_t>(
        std::count_if(merge_claims.begin(), merge_claims.end(),
                      [](std::uint8_t claims) { return claims >= 2; }));
  }

  void update_wide_merge_routes() {
    if (!spread_congested_routes || replan_each_tick ||
        endpoint_approach_obstructed()) {
      wide_merge_replan_queue.clear();
      return;
    }
    if (wide_merge_checked || !diversity_wave_attempted ||
        !routes_diversified || !diversity_replan_queue.empty() ||
        !replan_queue.empty()) {
      return;
    }
    const auto active = static_cast<std::size_t>(outstanding_goal_count());
    const auto threshold = std::max(std::size_t{8}, active / 16);
    if (active < 64 || last_movement_waits < threshold) {
      return;
    }
    // Inspect exactly one post-wave snapshot. Rechecking every later tick can
    // turn ordinary gate contention into an unnecessary second wave.
    wide_merge_checked = true;
    wide_merge_tiles = count_wide_merge_tiles();
    if (wide_merge_tiles < 64) {
      return;
    }
    ++wide_merge_replan_waves;
    wide_merge_replan_queue.request_all(agents);
  }

  // Example: bound expensive recovery work. A retry clock cannot distinguish
  // a jammed queue from a sealed goal, so the library schedule selects a
  // bounded, jittered subset for classification. The terrain graph answers
  // the cheap question first. Only when terrain remains connected do exact
  // `Traveler` and `Walker` searches distinguish a temporary settled-agent
  // obstruction from a durable terrain failure.
  auto recover_blocked_agents(std::uint64_t tick, std::size_t max_queries)
      -> std::size_t {
    auto recovery_options = kRecoveryOptions;
    recovery_options.max_probes_per_tick = max_queries;
    (void)recovery_schedule.collect_due(agents, tick, recovery_options);
    std::size_t processed_agents = 0;
    std::size_t exact_queries = 0;
    for (const auto index : recovery_schedule.due_agent_indices()) {
      if (exact_queries >= max_queries) {
        break;
      }
      ++processed_agents;
      auto& agent = agents[index];
      if (terrain_confirmation_pending[index] != 0) {
        const auto terrain_route = tess::astar_path<World, Walker>(
            world, tess::PathRequest{agent.position, agent.goal},
            settle_scratch);
        ++exact_queries;
        terrain_confirmation_pending[index] = 0;
        if (terrain_route.status == tess::PathStatus::Found) {
          crowd_blocked[index] = 1;
          tess::clear_path_agent_goal(tick_state, agent);
        } else if (terrain_route.status == tess::PathStatus::NoPath ||
                   terrain_route.status == tess::PathStatus::InvalidStart ||
                   terrain_route.status == tess::PathStatus::InvalidGoal) {
          crowd_blocked[index] = 0;
          tess::fail_path_agent_flow(agent, tick_state.flow_accounting);
          agent.phase = tess::PathAgentPhase::Unreachable;
          agent.status = terrain_route.status;
        }
        recovery_schedule.record_attempt(index, tick, recovery_options);
        continue;
      }
      if (tess::precheck_path<Walker>(
              graph, world, {agent.position, agent.goal}, graph_scratch) ==
          tess::PrecheckStatus::Unreachable) {
        terrain_confirmation_pending[index] = 0;
        crowd_blocked[index] = 0;
        tess::fail_path_agent_flow(agent, tick_state.flow_accounting);
        agent.phase = tess::PathAgentPhase::Unreachable;
        agent.status = tess::PathStatus::NoPath;
      } else {
        // A Blocked agent is not settled, but clear its marker defensively so
        // publication lag cannot make the exact query reject its start.
        const auto marked = world.field<SettledTag>(agent.position);
        world.field<SettledTag>(agent.position) = false;
        const auto route = tess::astar_path<World, Traveler>(
            world, tess::PathRequest{agent.position, agent.goal},
            settle_scratch);
        ++exact_queries;
        world.field<SettledTag>(agent.position) = marked;
        if (route.status == tess::PathStatus::NoPath ||
            route.status == tess::PathStatus::InvalidStart ||
            route.status == tess::PathStatus::InvalidGoal) {
          // Traveler differs from Walker only by excluding settled agents. A
          // Traveler failure therefore describes a durable terrain failure
          // only when an independent Walker search agrees. Otherwise the route
          // is blocked solely by teammates that the next wave will rearm.
          if (exact_queries >= max_queries) {
            terrain_confirmation_pending[index] = 1;
            continue;
          }
          const auto terrain_route = tess::astar_path<World, Walker>(
              world, tess::PathRequest{agent.position, agent.goal},
              settle_scratch);
          ++exact_queries;
          if (terrain_route.status == tess::PathStatus::Found) {
            // The route is blocked only by agents settled for this leg. Mark
            // the lifecycle quiescent so it cannot consume more movement or
            // recovery work; the sidecar keeps it distinct from a durable
            // terrain failure and lets the controller rearm the whole wave.
            crowd_blocked[index] = 1;
            tess::clear_path_agent_goal(tick_state, agent);
          } else if (terrain_route.status == tess::PathStatus::NoPath ||
                     terrain_route.status == tess::PathStatus::InvalidStart ||
                     terrain_route.status == tess::PathStatus::InvalidGoal) {
            crowd_blocked[index] = 0;
            tess::fail_path_agent_flow(agent, tick_state.flow_accounting);
            agent.phase = tess::PathAgentPhase::Unreachable;
            agent.status = terrain_route.status;
          }
        } else if (route.status == tess::PathStatus::Found &&
                   agent.status != tess::PathStatus::Found) {
          // Route rebuilding shares the same exact-query budget on later
          // ticks; do not wake the synchronous all-agent driver.
          (void)replan_queue.request(index, agent);
        }
      }
      recovery_schedule.record_attempt(index, tick, recovery_options);
    }
    max_recovery_probes = std::max(max_recovery_probes, processed_agents);
    return exact_queries;
  }

  struct AgentTaskFn {
    Impl* demo = nullptr;
    auto operator()(const tess::ScheduleTaskContext& context)
        -> tess::ScheduleTaskResult {
      // Presentation owns interpolation. Capture each fixed tick's endpoints
      // around logical movement; catch-up frames naturally retain only the
      // last pair, which is the pair rendered with the accumulator remainder.
      demo->snapshot_before_movement();
      demo->sync_settled_obstacles();
      demo->update_route_diversity(context.clock.tick);
      demo->update_wide_merge_routes();
      // Example: run bounded pathing before movement. The earlier Pathing
      // phase refreshes shared topology; this Movement task then performs its
      // bounded per-agent planning before it consumes retained routes and
      // commits occupancy.
      // Marked here, not in tick(): a frame may grant several fixed ticks,
      // and the toggle promises a replan on every one of them.
      if (demo->replan_each_tick) {
        // The synchronous diagnostic strategy already replans every active
        // agent. It still needs the bounded classifier: replanning alone
        // cannot distinguish a wall seal from settled-only snapshot NoPath.
        demo->replan_queue.clear();
        demo->diversity_replan_queue.clear();
        demo->wide_merge_replan_queue.clear();
        (void)demo->recover_blocked_agents(context.clock.tick,
                                           kMaxPlanningQueriesPerTick);
        // Found recovery results need no retained-route work because the
        // synchronous pass below immediately replaces every active route.
        demo->replan_queue.clear();
        tess::mark_pathing_dirty(demo->tick_state);
      } else {
        std::size_t planning_queries = 0;
        if (planning_queries < kMaxPlanningQueriesPerTick &&
            !demo->diversity_replan_queue.empty()) {
          const auto diversity_stats =
              tess::process_weighted_path_agent_replans<World, Traveler>(
                  demo->world, demo->agents, demo->tick_state.routes,
                  demo->diversity_replan_queue, demo->replan_scratch,
                  tess::PathAgentReplanOptions{
                      .max_requests =
                          kMaxPlanningQueriesPerTick - planning_queries,
                      .equal_cost_tie_seed = 0x434f4c4f4e59ULL,
                  });
          planning_queries += diversity_stats.submitted;
        }
        if (planning_queries < kMaxPlanningQueriesPerTick) {
          const auto wide_merge_stats =
              tess::process_weighted_path_agent_replans<World, Traveler>(
                  demo->world, demo->agents, demo->tick_state.routes,
                  demo->wide_merge_replan_queue, demo->replan_scratch,
                  tess::PathAgentReplanOptions{
                      .max_requests =
                          kMaxPlanningQueriesPerTick - planning_queries,
                      .equal_cost_tie_seed = 0x434f4e47455354ULL,
                  });
          planning_queries += wide_merge_stats.submitted;
        }
        if (planning_queries < kMaxPlanningQueriesPerTick) {
          const auto canonical_stats =
              tess::process_weighted_path_agent_replans<World, Traveler>(
                  demo->world, demo->agents, demo->tick_state.routes,
                  demo->replan_queue, demo->replan_scratch,
                  tess::PathAgentReplanOptions{
                      .max_requests =
                          kMaxPlanningQueriesPerTick - planning_queries,
                  });
          planning_queries += canonical_stats.submitted;
        }
        const auto remaining_queries =
            kMaxPlanningQueriesPerTick - planning_queries;
        const auto recovery_queries =
            demo->recover_blocked_agents(context.clock.tick, remaining_queries);
        demo->max_planning_queries = std::max(
            demo->max_planning_queries, planning_queries + recovery_queries);
      }
      auto options = tess::PathAgentTickOptions{};
      // This demo's exact replans are owned by replan_queue. Zero keeps a
      // Blocked/NoPath agent asleep until recovery enqueues it; retained
      // occupancy-blocked routes still retry movement every tick.
      options.max_blocked_retries = 0;
      options.blocked_exhaustion_policy =
          tess::BlockedAgentExhaustionPolicy::RemainBlocked;
      // No graph here, deliberately. The graph models terrain and is stamped
      // for `Walker`; `precheck_path` rejects a stamp mismatch outright
      // (GraphStale), so handing it to a `Traveler` search would never prune
      // anything and would only read as though it did. Keeping the graph on
      // terrain is still right -- rebuilding it as colonists settle would
      // churn topology over something that is not terrain -- so the precheck
      // it can soundly answer is made explicitly, in recover_blocked_agents.
      // Joint movement with SwapPolicy::Permit: these point-like colonists may
      // exchange tiles instead of allowing a head-on pair to wedge. The
      // library default remains Forbid, the standard MAPF constraint. Chains
      // and rotations are admitted under either policy, so convoys also drain
      // in one tick.
      const auto stats = tess::tick_weighted_path_agents_with_joint_movement<
          World, Traveler, kMaxCost, OccupancyTag, ReservationTag>(
          demo->tick_state, demo->world, demo->agents, demo->runtime,
          demo->joint_scratch, options,
          tess::JointMoveOptions{tess::SwapPolicy::Permit}, 0, nullptr);
      demo->last_advanced = stats.movement.advanced;
      demo->last_movement_waits = stats.movement.blocked_waits;
      // A colony can stop dead without any agent being durably blocked --
      // two agents each standing on the tile the other needs will block, be
      // refunded, and block again forever. Nobody reports that today, so the
      // page reads "Colony running" over a frozen grid. Count consecutive
      // ticks in which not one agent moved; the page turns that into a
      // stalled message. Arrival is progress, so a colony waiting out the
      // relaunch dwell is not stalled.
      if (stats.movement.advanced == 0 && demo->outstanding_goal_count() > 0) {
        ++demo->stalled_ticks;
      } else {
        demo->stalled_ticks = 0;
      }
      demo->snapshot_after_movement();
      return {};
    }
  };

  TopologyTaskFn topology_task{this};
  AgentTaskFn agent_task{this};

  // Declared after every task object it references: members are destroyed
  // in reverse declaration order, and the non-owning Schedule must go first.
  tess::Schedule schedule;

  [[nodiscard]] auto queue_wall(tess::Coord3 coord) -> bool {
    // Example: queue a world edit. Admission is synchronous, but mutation is
    // deferred to the PreUpdate AutoExec task so dirty publication, topology,
    // and movement retain one deterministic schedule order.
    // JavaScript and the Wasm model run on one thread: no fixed tick can move
    // an agent between this admission check and the next PreUpdate build. Keep
    // the invariant every other colony writer already follows -- construction
    // never turns an occupied source into impassable terrain.
    if (world.field<OccupancyTag>(coord)) {
      return false;
    }
    pending_walls.push_back(coord);
    const auto key = tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord));
    (void)ops.update_field(
        tess::DomainDesc::explicit_chunks({&key, 1}),
        tess::FieldAccessDesc{0, kTerrainDirty, kTerrainDirty},
        tess::WritePolicy::UniquePerChunk);
    return true;
  }

  // Example: consume a DeltaFrame as invalidation, not copied tile payload.
  // Covered tiles are re-read from the authoritative world into the shadow.
  [[nodiscard]] auto consume_frame(const tess::DeltaFrame& frame) -> bool {
    if (!tess::delta_frame_applicable(frame.header, version)) {
      return false;
    }
    const auto repaint = [&](tess::Coord3 coord) {
      const auto wall = world.field<ConstructionTag>(coord);
      shadow[static_cast<std::size_t>(coord.y) * kWidth +
             static_cast<std::size_t>(coord.x)] = wall ? 1 : 0;
    };
    for (const auto& chunk : frame.chunks()) {
      if (chunk.tile_count != 0) {
        for (std::uint32_t i = 0; i < chunk.tile_count; ++i) {
          repaint(frame.tiles()[chunk.first_tile + i].coord);
        }
      } else {
        const auto& box = chunk.bounds;
        for (auto y = box.origin.y;
             y < box.origin.y + static_cast<std::int64_t>(box.extent.y); ++y) {
          for (auto x = box.origin.x;
               x < box.origin.x + static_cast<std::int64_t>(box.extent.x);
               ++x) {
            repaint(tess::Coord3{x, y, 0});
          }
        }
      }
    }
    version = frame.header.to_version;
    return true;
  }

  void publish_render_frame() {
    tess::collect_tile_deltas(deltas, world, kTerrainDirty);
    if (consume_frame(deltas.publish())) {
      return;
    }

    // Example: recover a rejected DeltaFrame. A version gap or truncation is
    // structural, so skipping it and resuming incrementals cannot repair the
    // shadow. Publish a complete baseline and adopt its version instead.
    tess::collect_baseline(deltas, world, kTerrainDirty);
    if (!consume_frame(deltas.publish())) {
      TESS_ASSERT(false);
    }
  }

  // Advances the simulation by the measured real elapsed seconds. Returns
  // the average cost of one fixed tick in microseconds, or -1 when the
  // accumulator granted no tick this frame.
  auto tick(double dt_seconds) -> double {
    const auto begin = std::chrono::steady_clock::now();
    const auto summary =
        tess::run_schedule_frame(schedule, sim_clock, accumulator, dt_seconds,
                                 tess::SimTimeControl{tess::SimSpeed::Speed1x});
    publish_render_frame();
    interpolation_alpha = summary.alpha;
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_us =
        std::chrono::duration<double, std::micro>(end - begin).count();
    return summary.ticks > 0 ? elapsed_us / static_cast<double>(summary.ticks)
                             : -1.0;
  }

  auto arrived() const -> int {
    int count = 0;
    for (std::size_t i = 0; i < agents.size(); ++i) {
      if (!agents[i].has_goal && crowd_blocked[i] == 0) {
        ++count;
      }
    }
    return count;
  }

  auto unreachable() const -> int {
    int count = 0;
    for (std::size_t i = 0; i < agents.size(); ++i) {
      if (agents[i].phase == tess::PathAgentPhase::Unreachable &&
          crowd_blocked[i] == 0) {
        ++count;
      }
    }
    return count;
  }

  auto crowd_blocked_count() const -> int {
    int count = 0;
    for (const auto blocked : crowd_blocked) {
      count += blocked != 0 ? 1 : 0;
    }
    return count;
  }

  [[nodiscard]] auto turnaround_ready() const -> bool {
    return unreachable() == 0 &&
           arrived() + crowd_blocked_count() == static_cast<int>(agents.size());
  }

  auto outstanding_goal_count() const -> int {
    int count = 0;
    for (const auto& agent : agents) {
      if (tess::path_agent_goal_outstanding(agent)) {
        ++count;
      }
    }
    return count;
  }

  // Aborts a settled-blocked leg, or completes an all-arrived one, by arming
  // the whole colony toward the opposite side. Turning the synchronized wave
  // together avoids mixed-direction traffic and makes every settled tile
  // temporary without inventing sidestep goals or movement authority.
  auto relaunch() -> int {
    if (!turnaround_ready()) {
      return leg;
    }
    if (crowd_blocked_count() == 0) {
      ++completed_legs;
    } else {
      ++aborted_legs;
    }
    outbound = !outbound;
    diversity_wave_attempted = false;
    routes_diversified = false;
    diversity_replan_queue.clear();
    wide_merge_checked = false;
    wide_merge_tiles = 0;
    wide_merge_replan_queue.clear();
    last_advanced = 0;
    last_movement_waits = 0;
    recovery_schedule.clear();
    for (std::size_t i = 0; i < agents.size(); ++i) {
      crowd_blocked[i] = 0;
      terrain_confirmation_pending[i] = 0;
      tess::set_path_agent_goal(tick_state, agents[i],
                                outbound ? away_tile(i) : home_tile(i));
      // Exact planning is owned by the bounded FIFO. Keeping every freshly
      // armed agent asleep avoids a synchronous all-agent NeedsPath pass.
      agents[i].phase = tess::PathAgentPhase::Blocked;
    }
    replan_queue.request_all(agents);
    ++leg;
    return leg;
  }

  [[nodiscard]] auto current_leg() const noexcept -> int { return leg; }
  [[nodiscard]] auto completed_leg_count() const noexcept -> int {
    return completed_legs;
  }
  [[nodiscard]] auto aborted_leg_count() const noexcept -> int {
    return aborted_legs;
  }
};

// Narrow white-box access for the native verification translation unit. The
// browser-facing model header neither declares verification functions nor
// exposes the implementation pointer itself.
struct ColonyModelNativeAccess {
  [[nodiscard]] static auto impl(ColonyModel& model) noexcept
      -> ColonyModel::Impl& {
    return *model.impl_;
  }
};

}  // namespace tess::examples::web_colony

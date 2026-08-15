// The S2 colony macro-harness (redesign section 3.1): N agents with
// goals driven through the production stack — schedule loop, queued
// ops with result channels, path agents with movement, region-graph
// topology, and render deltas — parameterized by agent count, churn,
// executor and worker count, world size, and field payload width.
//
// Terrain comes from the S1 layer (grid_map_generators.h): a logical
// 64x64 room-and-corridor map raster-scaled into the world, so the
// same logical topology carries across the world-size axis.
//
// Harness support only, never a public header. Everything the
// harness decides derives from the configured seed through explicit
// SplitMix64 streams — separate streams for costs, agents, goals, and
// churn, so changing one axis cannot silently shift another.
#pragma once

#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "grid_benchmark_harness.h"
#include "grid_map_generators.h"

namespace tess_test::colony {

namespace grid = tess_test::grid_benchmark;

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

// The colonist movement class: passable ground, field-weighted cost.
using Walker =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;
inline constexpr std::uint32_t kMaxCost = 4;

// Terrain and occupancy carry distinct dirty masks: an agent step must
// never trigger a terrain topology rebuild or a world-scoped replan.
inline constexpr std::uint32_t kTerrainDirty = 1U << 0U;
inline constexpr std::uint32_t kOccupancyDirty = 1U << 1U;

// The logical map is always 64x64; the world extent must be a whole
// multiple of it so the raster scale is exact.
inline constexpr std::size_t kLogicalExtent = 64;

struct ColonyConfig {
  std::size_t agents = 100;
  std::uint64_t seed = 0x5C0107;
  std::uint32_t ticks = 60;
  // 0 disables churn. Otherwise a churn event fires every N ticks and
  // enqueues one operation per touched chunk.
  std::uint32_t churn_period = 0;
  std::uint32_t churn_chunks = 4;
  // 0 selects the serial executor; otherwise a worker pool of this
  // width backs the queued-op task.
  std::size_t worker_count = 0;
  // Clears the route and field-product caches on every world change,
  // the cold-cache run mode.
  bool cold_cache = false;
  // Bounded route length: goals sit this many tiles from their start
  // along +x, which keeps searches cheap and routes non-crossing.
  std::int64_t goal_distance = 24;
  // Rebuilds the region graph from scratch before sampling instead of
  // keeping the incrementally updated one. The two must answer
  // identically (section 3.2's incremental == fresh recompute gate).
  bool rebuild_graph_before_sampling = false;
  // Compares the incrementally updated graph against a freshly built
  // shadow graph after every churn event, BEFORE agents move that
  // tick. End-of-run sampling alone cannot catch an intermediate
  // graph that is wrong and later self-heals.
  bool verify_fresh_graph_each_churn = false;
  // Row stride of the scan-order agent placement. 0 keeps the
  // historical default of kSizeY / 64 (8 rows apart on the 512-tile
  // world, which seats at most 227 agents on the default map);
  // smaller strides visit more rows and seat larger populations.
  std::int64_t placement_stride = 0;
};

// Scenario-level counters. PathAgentTickStats::repaths_requested only
// counts blocked agents retrying an invalidated route, so it cannot
// answer "did churn force a replan"; these do.
struct ColonyCounters {
  std::uint64_t world_replan_passes = 0;
  std::uint64_t churn_events = 0;
  std::uint64_t churn_operations = 0;
  std::uint64_t churn_acked_tiles = 0;
  std::uint64_t fresh_graph_comparisons = 0;
  std::uint64_t fresh_graph_mismatches = 0;
  std::uint64_t blocked_route_repaths = 0;
  std::uint64_t blocked_retry_exhaustions = 0;
  std::uint64_t executed_runs = 0;
  std::uint64_t pool_phases = 0;
  std::uint64_t delta_publishes = 0;
};

struct ColonyRun {
  std::uint64_t ticks = 0;
  // Requested minus placed. The placement scan can run out of
  // qualifying tiles before reaching the requested population, and a
  // silently smaller colony would weaken every assertion below it.
  std::size_t agents_unplaced = 0;
  // Agents with no goal at end of run: arrivals plus retry
  // exhaustions. The harness does not reassign goals, so a run longer
  // than the initial routes idles — visible here rather than as a
  // mysteriously flat step count.
  std::size_t agents_idle_at_end = 0;
  std::size_t arrivals = 0;
  std::size_t total_steps = 0;
  std::vector<tess::Coord3> final_positions;
  // Sampled reachability answers from the region graph at end of run,
  // used for the incremental-versus-fresh differential.
  std::vector<std::uint8_t> sampled_reachability;
  tess::diagnostics::FlowCounters agent_flow;
  ColonyCounters counters;
};

struct BuildAck {
  std::size_t tiles = 0;
};

// One deterministic terrain edit: block a single tile.
struct TileEdit {
  tess::Coord3 coord;
};

/// The colony scenario over one world shape and field schema.
///
/// `Schema` must provide the passable, cost, occupancy, and
/// reservation fields; additional fields widen the payload, which is
/// the compile-time axis of section 3.1's parameter matrix.
template <typename Shape, typename Schema>
class Colony {
 public:
  using World = tess::AlwaysResidentWorld<Shape, Schema>;

  static constexpr std::size_t scale() {
    return static_cast<std::size_t>(Shape::size.x) / kLogicalExtent;
  }

  static_assert(static_cast<std::size_t>(Shape::size.x) ==
                    kLogicalExtent * scale(),
                "world extent must be a whole multiple of the logical map");
  static_assert(Shape::size.x == Shape::size.y,
                "the colony scenario uses square worlds");
  static_assert(scale() >= 2,
                "churn blocks a scaled block's centre tile, which only "
                "provably keeps the world connected when the block is at "
                "least 2x2");

  explicit Colony(ColonyConfig config) : config_(config) {}

  /// Runs the configured scenario and returns its observable result.
  auto run() -> ColonyRun;

  /// The logical map this configuration builds terrain from, exposed
  /// so tests can pin endpoints across world sizes.
  [[nodiscard]] static auto logical_map(std::uint64_t seed)
      -> grid::BenchmarkMap {
    const auto text = grid::room_and_corridor(kLogicalExtent, kLogicalExtent,
                                              seed, {24, 6, 12})
                          .value_or(grid::RoomMapResult{})
                          .text;
    return grid::parse_map("colony-logical", text).value;
  }

 private:
  ColonyConfig config_;
};

namespace detail {

// Deterministic per-tile cost in 1..kMaxCost. Cost 0 reads as blocked,
// so every passable tile must carry a positive weight or the world is
// immobile.
inline auto tile_cost(std::uint64_t seed, std::int64_t x, std::int64_t y)
    -> std::uint32_t {
  grid::SplitMix64 rng(seed ^ (static_cast<std::uint64_t>(x) << 32U) ^
                       static_cast<std::uint64_t>(y));
  return static_cast<std::uint32_t>(1 + rng.below(kMaxCost));
}

// A logical cell whose own cell and four neighbours are all passable:
// its scaled block's interior tile can be blocked without ever
// disconnecting the world, because the block's other tiles remain and
// carry every crossing.
inline auto interior_logical_cells(const grid::BenchmarkMap& map)
    -> std::vector<std::size_t> {
  std::vector<std::size_t> cells;
  for (std::size_t y = 1; y + 1 < map.height; ++y) {
    for (std::size_t x = 1; x + 1 < map.width; ++x) {
      const auto index = y * map.width + x;
      const bool open = map.passability[index] != 0 &&
                        map.passability[index - 1] != 0 &&
                        map.passability[index + 1] != 0 &&
                        map.passability[index - map.width] != 0 &&
                        map.passability[index + map.width] != 0;
      if (open) {
        cells.push_back(index);
      }
    }
  }
  return cells;
}

}  // namespace detail

template <typename Shape, typename Schema>
auto Colony<Shape, Schema>::run() -> ColonyRun {
  using WorldType = World;
  constexpr auto kScale = static_cast<std::int64_t>(scale());
  constexpr auto kSizeX = static_cast<std::int64_t>(Shape::size.x);
  constexpr auto kSizeY = static_cast<std::int64_t>(Shape::size.y);

  const auto map = logical_map(config_.seed);
  ColonyRun result;

  auto world = std::make_unique<WorldType>();
  // Terrain: raster-scale the logical map, then weight every passable
  // tile so movement can actually cross it.
  for (std::int64_t y = 0; y < kSizeY; ++y) {
    for (std::int64_t x = 0; x < kSizeX; ++x) {
      const auto logical_index =
          static_cast<std::size_t>(y / kScale) * map.width +
          static_cast<std::size_t>(x / kScale);
      const bool passable = map.passability[logical_index] != 0;
      const auto coord = tess::Coord3{x, y, 0};
      world->template field<PassableTag>(coord) = passable;
      world->template field<CostTag>(coord) =
          passable ? detail::tile_cost(config_.seed ^ 0xC057U, x, y) : 0U;
      world->template field<OccupancyTag>(coord) = false;
      world->template field<ReservationTag>(coord) = false;
    }
  }

  // Agents: scan-order placement on distinct passable tiles whose goal
  // (goal_distance tiles along +x) is also passable. Every agent moves
  // in the same direction, so no pair is ever head-on.
  std::vector<tess::PathAgentState> agents;
  std::vector<tess::Coord3> assigned_goals;
  agents.reserve(config_.agents);
  assigned_goals.reserve(config_.agents);
  const std::int64_t stride = config_.placement_stride > 0
                                  ? config_.placement_stride
                                  : std::max<std::int64_t>(1, kSizeY / 64);
  for (std::int64_t y = 1; y < kSizeY && agents.size() < config_.agents;
       y += stride) {
    for (std::int64_t x = 1;
         x + config_.goal_distance < kSizeX && agents.size() < config_.agents;
         x += config_.goal_distance + 2) {
      const auto start = tess::Coord3{x, y, 0};
      const auto goal = tess::Coord3{x + config_.goal_distance, y, 0};
      if (!world->template field<PassableTag>(start) ||
          !world->template field<PassableTag>(goal) ||
          world->template field<OccupancyTag>(start)) {
        continue;
      }
      tess::PathAgentState agent;
      agent.position = start;
      world->template field<OccupancyTag>(start) = true;
      agents.push_back(agent);
      assigned_goals.push_back(goal);
    }
  }

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(agents.size());
  runtime.reserve_search_nodes(static_cast<std::size_t>(kSizeX * kSizeY));
  runtime.reserve_path_nodes(agents.size() * 64);
  runtime.reserve_unit_routes(agents.size());

  tess::diagnostics::FlowAccounting flow;
  tess::PathAgentTickState tick_state;
  // Attach the accountant before arming any goal so no admission is
  // lost, and keep it attached through the snapshot.
  tick_state.flow_accounting = &flow;

  tess::LocalTopologyScratch topo_scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<WorldType, Walker>(*world, topo_scratch, graph);

  for (std::size_t i = 0; i < agents.size(); ++i) {
    tess::set_path_agent_goal(tick_state, agents[i], assigned_goals[i]);
  }

  tess::FrameOps ops;
  ops.reserve_operations(config_.churn_chunks + 4);

  // Churn candidates: interior tiles of fully-open scaled blocks, in
  // scan order, excluding agent starts and goals.
  const auto interior = detail::interior_logical_cells(map);
  std::vector<TileEdit> churn_pool;
  {
    grid::SplitMix64 rng(config_.seed ^ 0xC17U);
    std::vector<char> reserved(static_cast<std::size_t>(kSizeX * kSizeY), 0);
    const auto mark = [&](tess::Coord3 coord) {
      reserved[static_cast<std::size_t>(coord.y * kSizeX + coord.x)] = 1;
    };
    for (const auto& agent : agents) {
      mark(agent.position);
    }
    for (const auto& goal : assigned_goals) {
      mark(goal);
    }
    // Enough candidates for the whole run: a fixed cap would silently
    // stop churning partway through a long scenario.
    const std::size_t wanted =
        config_.churn_period == 0
            ? 0
            : (static_cast<std::size_t>(config_.ticks / config_.churn_period) +
               1) *
                  config_.churn_chunks;
    for (std::size_t attempt = 0;
         attempt < interior.size() && churn_pool.size() < wanted; ++attempt) {
      const auto logical = interior[rng.below(interior.size())];
      const auto lx = static_cast<std::int64_t>(logical % map.width);
      const auto ly = static_cast<std::int64_t>(logical / map.width);
      // The block's centre tile: blocking it cannot disconnect the
      // block, which is fully passable and at least 2x2 at any scale.
      const auto coord =
          tess::Coord3{lx * kScale + kScale / 2, ly * kScale + kScale / 2, 0};
      const auto flat = static_cast<std::size_t>(coord.y * kSizeX + coord.x);
      if (reserved[flat] != 0) {
        continue;
      }
      reserved[flat] = 1;
      churn_pool.push_back(TileEdit{coord});
    }
  }

  std::vector<TileEdit> pending_edits;
  std::size_t churn_cursor = 0;

  auto build_fn = [&pending_edits](auto view, BuildAck& ack) {
    auto passable = view.template field_span<PassableTag>();
    auto cost = view.template field_span<CostTag>();
    for (const auto& edit : pending_edits) {
      if (tess::chunk_key<Shape>(tess::chunk_coord<Shape>(edit.coord)) !=
          view.key()) {
        continue;
      }
      const auto local =
          tess::local_tile_id<Shape>(tess::local_coord<Shape>(edit.coord));
      passable[local.value] = false;
      cost[local.value] = 0U;
      ++ack.tiles;
    }
  };

  tess::AutoExecTask<WorldType, tess::WritePolicy::UniquePerChunk, BuildAck,
                     decltype(build_fn)>
      build_task(*world, ops, build_fn);
  build_task.reserve_operations(config_.churn_chunks + 4);
  build_task.set_result_hook(&result, [](void* ctx, tess::OpHandle,
                                         const tess::OpCompletion& done,
                                         const BuildAck* ack) noexcept {
    if (done.ok() && ack != nullptr) {
      static_cast<ColonyRun*>(ctx)->counters.churn_acked_tiles += ack->tiles;
    }
  });
  std::optional<tess::WorkerPoolPhaseExecutor> pool;
  if (config_.worker_count > 0) {
    pool.emplace(config_.worker_count);
    pool->reserve_operations(config_.churn_chunks + 4);
    // Threshold 2: a churn event always enqueues at least two
    // operations, so the pool genuinely runs.
    build_task.use_pool(*pool, 2);
  }

  struct TopologyTask {
    WorldType* world = nullptr;
    tess::LocalTopologyScratch* scratch = nullptr;
    tess::RegionGraph* graph = nullptr;
    tess::PathAgentTickState* tick_state = nullptr;
    ColonyRun* result = nullptr;
    bool cold_cache = false;
    bool verify_fresh = false;
    tess::PathRequestRuntime* runtime = nullptr;
    const std::vector<std::pair<tess::Coord3, tess::Coord3>>* probes = nullptr;
    std::vector<tess::ChunkKey> dirty_scratch;

    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      dirty_scratch.clear();
      world->collect_dirty_chunks(kTerrainDirty, dirty_scratch);
      if (!dirty_scratch.empty()) {
        (void)tess::update_region_graph<WorldType, Walker>(
            *world, *scratch, *graph, dirty_scratch);
        // Only the topology task marks pathing dirty, and only after
        // the graph is current: a replan against a stale graph would
        // route through terrain that no longer exists.
        tess::mark_pathing_dirty(*tick_state);
        ++result->counters.world_replan_passes;
        if (cold_cache) {
          runtime->clear_caches();
        }
        if (verify_fresh && probes != nullptr) {
          // Section 3.2's differential checked while it still matters:
          // a wrong intermediate graph changes THIS tick's routes even
          // if a later rebuild heals it, so comparing only at the end
          // of the run would miss it.
          tess::LocalTopologyScratch fresh_scratch;
          tess::RegionGraph fresh_graph;
          tess::build_region_graph<WorldType, Walker>(*world, fresh_scratch,
                                                      fresh_graph);
          tess::RegionGraphScratch reach_scratch;
          for (const auto& probe : *probes) {
            const auto live = tess::reachable<Shape>(
                *graph, {probe.first, probe.second}, reach_scratch);
            const auto fresh = tess::reachable<Shape>(
                fresh_graph, {probe.first, probe.second}, reach_scratch);
            ++result->counters.fresh_graph_comparisons;
            if (live.status != fresh.status) {
              ++result->counters.fresh_graph_mismatches;
            }
          }
        }
      }
      return {};
    }
  };

  struct AgentTask {
    WorldType* world = nullptr;
    std::span<tess::PathAgentState> agents;
    tess::PathRequestRuntime* runtime = nullptr;
    tess::PathAgentTickState* tick_state = nullptr;
    ColonyRun* result = nullptr;
    const tess::RegionGraph* graph = nullptr;

    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      const auto stats = tess::tick_weighted_path_agents_with_movement<
          WorldType, Walker, kMaxCost, OccupancyTag, ReservationTag>(
          *tick_state, *world, agents, *runtime,
          {.movement_dirty_mask = kOccupancyDirty}, graph);
      result->counters.blocked_route_repaths += stats.repaths_requested;
      result->counters.blocked_retry_exhaustions += stats.repath_exhausted;
      return {};
    }
  };

  // Probe pairs for the per-churn differential: each agent's start
  // and goal, fixed up front so the comparison is stable.
  std::vector<std::pair<tess::Coord3, tess::Coord3>> probes;
  probes.reserve(agents.size());
  for (std::size_t i = 0; i < agents.size(); ++i) {
    probes.emplace_back(agents[i].position, assigned_goals[i]);
  }

  TopologyTask topology_task{world.get(),
                             &topo_scratch,
                             &graph,
                             &tick_state,
                             &result,
                             config_.cold_cache,
                             config_.verify_fresh_graph_each_churn,
                             &runtime,
                             &probes,
                             {}};
  AgentTask agent_task{world.get(), std::span<tess::PathAgentState>{agents},
                       &runtime,    &tick_state,
                       &result,     &graph};

  tess::Schedule schedule;
  schedule.reserve_tasks(3);
  (void)schedule.add_task(
      {"build", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
      build_task);
  // Pathing precedes Movement; Topology does not. Registering the
  // rebuild in Pathing gives commit -> rebuild -> replan -> move
  // within one tick.
  (void)schedule.add_task({"topology", tess::SimPhase::Pathing,
                           tess::Cadence::on_dirty(kTerrainDirty)},
                          topology_task);
  (void)schedule.add_task(
      {"agents", tess::SimPhase::Movement, tess::Cadence::every_tick()},
      agent_task);
  schedule.seal();

  tess::DeltaCollector deltas;
  deltas.reserve(WorldType::chunk_count, 4096, 64);

  tess::SimClock clock;
  tess::FixedStepAccumulator accumulator(20, 8);
  std::vector<tess::Coord3> previous(agents.size());
  for (std::size_t i = 0; i < agents.size(); ++i) {
    previous[i] = agents[i].position;
  }

  for (std::uint32_t tick = 0; tick < config_.ticks; ++tick) {
    // Inventory accounting is driven once per tick, before this
    // tick's transitions.
    tess::observe_path_agent_flow_tick(tick_state, agents, tick);

    pending_edits.clear();
    if (config_.churn_period > 0 && tick > 0 &&
        tick % config_.churn_period == 0 && churn_cursor < churn_pool.size()) {
      // One operation per DISTINCT chunk: the auto-exec task selects
      // the pool by phase operation count, so a single multi-chunk
      // operation would never engage it.
      // The edit script is chosen by COORDINATE, independently of the
      // chunk decomposition: consuming candidates while deduplicating
      // by ChunkKey would make two chunk shapes block different tiles,
      // and the chunk-size invariance test would then be comparing two
      // different scenarios.
      for (std::uint32_t taken = 0;
           taken < config_.churn_chunks && churn_cursor < churn_pool.size();
           ++churn_cursor) {
        const auto& edit = churn_pool[churn_cursor];
        // An agent standing here would be walled in mid-route. Setup
        // excluded starts and goals, but occupancy moves.
        if (world->template field<OccupancyTag>(edit.coord)) {
          continue;
        }
        pending_edits.push_back(edit);
        ++taken;
      }
      // One operation per distinct chunk covering that script: the
      // auto-exec task selects the pool by phase operation count, so a
      // single multi-chunk operation would never engage it. The
      // operation count may differ across chunk shapes; the terrain
      // the script produces does not.
      std::vector<tess::ChunkKey> keys;
      for (const auto& edit : pending_edits) {
        const auto key =
            tess::chunk_key<Shape>(tess::chunk_coord<Shape>(edit.coord));
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
          keys.push_back(key);
        }
      }
      for (const auto& key : keys) {
        (void)ops.update_field(
            tess::DomainDesc::explicit_chunks(
                std::span<const tess::ChunkKey>{&key, 1}),
            tess::FieldAccessDesc{0, kTerrainDirty, kTerrainDirty},
            tess::WritePolicy::UniquePerChunk);
        ++result.counters.churn_operations;
      }
      if (!pending_edits.empty()) {
        ++result.counters.churn_events;
      }
    }

    (void)tess::run_schedule_frame(
        schedule, clock, accumulator, 1.0 / 20.0,
        tess::SimTimeControl{tess::SimSpeed::Speed1x});

    const auto& run_stats = build_task.last_run();
    if (run_stats.status == tess::AutoExecStatus::Executed) {
      ++result.counters.executed_runs;
      result.counters.pool_phases += run_stats.pool_phases;
    }

    for (std::size_t i = 0; i < agents.size(); ++i) {
      if (agents[i].position != previous[i]) {
        ++result.total_steps;
        previous[i] = agents[i].position;
      }
    }

    // Terrain deltas only: agent movement is committed through
    // occupancy under kOccupancyDirty and is not published here, so
    // delta_publishes counts terrain edits rather than every visible
    // change.
    tess::collect_tile_deltas(deltas, *world, kTerrainDirty);
    const auto frame = deltas.publish();
    if (!frame.empty()) {
      ++result.counters.delta_publishes;
    }
  }

  // Section 3.2's differential: the incrementally updated graph must
  // answer exactly like one rebuilt from scratch over the same final
  // terrain. Sampling happens on whichever graph the configuration
  // selected, so the test compares the two runs.
  if (config_.rebuild_graph_before_sampling) {
    graph = tess::RegionGraph{};
    tess::build_region_graph<WorldType, Walker>(*world, topo_scratch, graph);
  }
  result.sampled_reachability.reserve(agents.size() * 2);
  {
    // Sample each agent against its own goal and against a blocked
    // tile, so the answer vector carries both outcomes: a graph that
    // answered uniformly would otherwise pass the differential.
    tess::Coord3 blocked{0, 0, 0};
    for (std::int64_t y = 0; y < kSizeY; ++y) {
      for (std::int64_t x = 0; x < kSizeX; ++x) {
        const auto coord = tess::Coord3{x, y, 0};
        if (!world->template field<PassableTag>(coord)) {
          blocked = coord;
          y = kSizeY;
          break;
        }
      }
    }
    tess::RegionGraphScratch reach_scratch;
    for (std::size_t i = 0; i < agents.size(); ++i) {
      const auto to_goal = tess::reachable<Shape>(
          graph, {agents[i].position, assigned_goals[i]}, reach_scratch);
      result.sampled_reachability.push_back(
          static_cast<std::uint8_t>(to_goal.status));
      const auto to_blocked = tess::reachable<Shape>(
          graph, {agents[i].position, blocked}, reach_scratch);
      result.sampled_reachability.push_back(
          static_cast<std::uint8_t>(to_blocked.status));
    }
  }

  result.ticks = config_.ticks;
  result.agents_unplaced = config_.agents - agents.size();
  result.final_positions.reserve(agents.size());
  for (std::size_t i = 0; i < agents.size(); ++i) {
    result.final_positions.push_back(agents[i].position);
    if (!agents[i].has_goal) {
      ++result.agents_idle_at_end;
      if (agents[i].position == assigned_goals[i]) {
        ++result.arrivals;
      }
    }
  }
  result.agent_flow = flow.counters;
  return result;
}

}  // namespace tess_test::colony

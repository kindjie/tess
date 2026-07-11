// The flagship composition: a colony sim frame loop where queued
// construction edits run through the auto-exec schedule task, an OnDirty
// task rebuilds the region topology exactly when terrain changed, agents
// route through a movement class that refuses construction sites, and a
// render consumer applies versioned DeltaFrames into a shadow grid.
// Everything is driven by one tess::Schedule through run_schedule_frame.
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <vector>

namespace {

struct PassableTag {};
struct ConstructionTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<ConstructionTag, bool>,
    tess::Field<CostTag, std::uint32_t>, tess::Field<OccupancyTag, bool>,
    tess::Field<ReservationTag, bool>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

// Colonists walk passable ground that is NOT a construction site.
using Walker = tess::movement::MovementClass<
    tess::movement::AllOf<
        tess::movement::Field<PassableTag>,
        tess::movement::Not<tess::movement::Field<ConstructionTag>>>,
    tess::movement::FieldCost<CostTag>>;
constexpr std::uint32_t kMaxCost = 4;

constexpr std::uint32_t kTerrainDirty = 1U << 0U;

// One queued construction order: a wall segment along y in column `x`.
struct WallOrder {
  std::int64_t x = 0;
  std::int64_t y_begin = 0;
  std::int64_t y_end = 0;
};

struct BuildAck {
  std::size_t tiles = 0;
};

// The colony: world, agents, topology, schedule tasks, render collector.
struct Colony {
  World world;
  std::array<tess::PathAgentState, 4> agents{};
  tess::PathRequestRuntime runtime;
  tess::PathAgentTickState tick_state;
  tess::LocalTopologyScratch topo_scratch;
  tess::RegionGraph graph;
  tess::FrameOps ops;
  WallOrder pending_wall{};
  tess::DeltaCollector deltas;
  std::size_t built_tiles = 0;
};

// EveryTick agent task: weighted Walker routing with the region-graph
// precheck, committing each step through occupancy.
struct AgentTask {
  Colony* colony = nullptr;

  auto operator()(const tess::ScheduleTaskContext&)
      -> tess::ScheduleTaskResult {
    (void)tess::tick_weighted_path_agents_with_movement<
        World, Walker, kMaxCost, OccupancyTag, ReservationTag>(
        colony->tick_state, colony->world, colony->agents, colony->runtime, {},
        0, &colony->graph);
    return {};
  }
};

// OnDirty(terrain) task: incremental per-class topology update over the
// dirty chunks, then re-path every agent -- runs exactly on the ticks
// where the auto-exec task landed construction.
struct TopologyTask {
  Colony* colony = nullptr;
  std::vector<tess::ChunkKey> dirty_scratch{};

  auto operator()(const tess::ScheduleTaskContext&)
      -> tess::ScheduleTaskResult {
    dirty_scratch.clear();
    colony->world.collect_dirty_chunks(kTerrainDirty, dirty_scratch);
    if (!dirty_scratch.empty()) {
      (void)tess::update_region_graph<World, Walker>(
          colony->world, colony->topo_scratch, colony->graph, dirty_scratch);
      tess::mark_pathing_dirty(colony->tick_state);
    }
    return {};
  }
};

auto run() -> int {
  Colony colony;
  for (auto& page : colony.world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
      cost[i] = 1u;
    }
  }
  colony.runtime.reserve_requests(colony.agents.size());
  colony.runtime.reserve_search_nodes(1024);
  colony.runtime.reserve_path_nodes(4096);
  colony.runtime.reserve_unit_routes(colony.agents.size());
  colony.deltas.reserve(World::chunk_count, 2048, 16);
  tess::build_region_graph<World, Walker>(colony.world, colony.topo_scratch,
                                          colony.graph);

  // Colonists set out across the map; the wall will land in their way.
  for (std::size_t i = 0; i < colony.agents.size(); ++i) {
    auto& agent = colony.agents[i];
    agent.position = tess::Coord3{2, static_cast<std::int64_t>(6 + i * 6), 0};
    colony.world.field<OccupancyTag>(agent.position) = true;
    tess::set_path_agent_goal(colony.tick_state, agent,
                              tess::Coord3{29, agent.position.y, 0});
  }

  // The auto-exec construction task: queued ops apply the pending wall
  // order, one UniquePerChunk op per touched chunk, acks counted.
  auto build_fn = [&colony](auto view, BuildAck& ack) {
    const auto& wall = colony.pending_wall;
    auto passable = view.template field_span<PassableTag>();
    auto construction = view.template field_span<ConstructionTag>();
    for (auto y = wall.y_begin; y < wall.y_end; ++y) {
      const auto coord = tess::Coord3{wall.x, y, 0};
      if (tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord)) !=
          view.key()) {
        continue;
      }
      const auto local =
          tess::local_tile_id<Shape>(tess::local_coord<Shape>(coord));
      passable[local.value] = false;
      construction[local.value] = true;
      ++ack.tiles;
    }
  };
  tess::AutoExecTask<World, tess::WritePolicy::UniquePerChunk, BuildAck,
                     decltype(build_fn)>
      build_task(colony.world, colony.ops, build_fn);
  build_task.reserve_operations(8);
  build_task.set_result_hook(
      &colony, [](void* ctx, tess::OpHandle, const tess::OpCompletion& done,
                  const BuildAck* ack) {
        if (done.ok() && ack != nullptr) {
          static_cast<Colony*>(ctx)->built_tiles += ack->tiles;
        }
      });

  AgentTask agent_task{&colony};
  TopologyTask topology_task{&colony};
  tess::Schedule schedule;
  schedule.reserve_tasks(3);
  (void)schedule.add_task(
      {"build", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
      build_task);
  (void)schedule.add_task({"topology", tess::SimPhase::Topology,
                           tess::Cadence::on_dirty(kTerrainDirty)},
                          topology_task);
  (void)schedule.add_task(
      {"agents", tess::SimPhase::Movement, tess::Cadence::every_tick()},
      agent_task);
  schedule.seal();

  tess::SimClock clock;
  tess::FixedStepAccumulator accumulator(20, 8);
  auto ordered = false;
  auto detoured = false;
  for (int frame = 0; frame < 80; ++frame) {
    if (frame == 4 && !ordered) {
      // The colony orders a wall straight across the colonists' routes:
      // a vertical segment at x=14 covering every agent row (y 4..27),
      // open at both ends so the detour exists.
      ordered = true;
      colony.pending_wall = WallOrder{14, 4, 28};
      for (std::int64_t chunk_y = 0; chunk_y < 4; ++chunk_y) {
        const auto key = tess::chunk_key<Shape>(tess::chunk_coord<Shape>(
            tess::Coord3{colony.pending_wall.x, chunk_y * 8, 0}));
        (void)colony.ops.update_field(
            tess::DomainDesc::explicit_chunks(
                std::span<const tess::ChunkKey>{&key, 1}),
            tess::FieldAccessDesc{0, kTerrainDirty, kTerrainDirty},
            tess::WritePolicy::UniquePerChunk);
      }
      std::cout << "frame 4: wall ordered across the corridor\n";
    }
    (void)tess::run_schedule_frame(
        schedule, clock, accumulator, 1.0 / 20.0,
        tess::SimTimeControl{tess::SimSpeed::Speed1x});
    // Detour evidence: every start and goal sits on rows 6..24, so any
    // agent seen at the wall's open ends got there by routing around it.
    for (const auto& agent : colony.agents) {
      if (agent.position.y <= 3 || agent.position.y >= 28) {
        detoured = true;
      }
    }
    tess::collect_tile_deltas(colony.deltas, colony.world, kTerrainDirty);
    const auto delta_frame = colony.deltas.publish();
    if (!delta_frame.empty()) {
      std::cout << "frame " << frame << ": render frame v"
                << delta_frame.header.to_version.value << " invalidated "
                << delta_frame.chunks.size() << " chunks\n";
    }
  }

  std::cout << "wall tiles built: " << colony.built_tiles << "\n";
  auto arrived = 0;
  for (const auto& agent : colony.agents) {
    if (!agent.has_goal && agent.position.x == 29) {
      ++arrived;
    }
  }
  std::cout << "colonists home: " << arrived << "/" << colony.agents.size()
            << (detoured ? " (routed around the construction)\n"
                         : " (never detoured?!)\n");
  if (colony.built_tiles != 24 || arrived != 4 || !detoured) {
    std::cerr << "colony did not converge around the wall\n";
    return 1;
  }
  return 0;
}

}  // namespace

auto main() -> int {
  try {
    return run();
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}

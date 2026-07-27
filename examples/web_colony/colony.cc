// Browser build of the colony_2d composition: queued wall construction, an
// OnDirty topology rebuild, movement-class agents, and a DeltaFrame-driven
// shadow grid, exported to JavaScript. Compiled natively it self-checks the
// same model (see main below); compiled with Emscripten it becomes the
// /demo/colony/ interactive page.

#include <tess/pathfinding.h>
#include <tess/simulation.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_DEMO_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_DEMO_EXPORT
#endif

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

namespace {

struct PassableTag {};
struct ConstructionTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};
// Set on the tile of every agent that will not move again -- one that has
// arrived, or one the library has declared terminal. Distinct from occupancy,
// which a travelling agent also sets and vacates a tick later.
struct SettledTag {};

constexpr int kWidth = 128;
constexpr int kHeight = 128;
constexpr int kMaxAgents = 1024;
// Wall painting is rejected outside this band so the spawn columns on the
// left and the turnaround columns on the right always stay standable.
constexpr int kWallMinX = 10;
constexpr int kWallMaxX = kWidth - 11;
// Consecutive blocked ticks an agent may spend before the demo re-examines it.
// Terminal is then decided by an actual search rather than by the clock (see
// Demo::refresh_settled_agents), so this only has to be long enough that
// ordinary convoy shuffling never pays for that search.
constexpr std::uint32_t kMaxBlockedRetries = 32;

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
// would thrash routes for nothing -- but a settled agent never moves again,
// which makes its tile as solid as a wall to everyone else. Leaving it out of
// planning deadlocks any agent whose goal lies beyond one (a bottleneck makes
// that routine: see the regression scenario in main).
using Traveler = tess::movement::MovementClass<
    tess::movement::AllOf<
        tess::movement::Field<PassableTag>,
        tess::movement::Not<tess::movement::Field<ConstructionTag>>,
        tess::movement::Not<tess::movement::Field<SettledTag>>>,
    tess::movement::FieldCost<CostTag>>;
constexpr std::uint32_t kMaxCost = 4;
constexpr std::uint32_t kTerrainDirty = 1U << 0U;

struct BuildAck {
  std::size_t tiles = 0;
};

// Convoy layout: batch k = i / kHeight walks row y = i % kHeight between
// column 9 - k (home) and kWidth - 3 - k (away). Every agent owns a distinct
// goal tile — occupancy admits exactly one occupant, so shared goals would
// leave all but one agent blocked forever — and every trip has equal length,
// with the outbound leader (k = 0) starting ahead of its followers so nobody
// parks in front of a teammate still travelling.
//
// That last clause holds only while each agent keeps to its own row. A painted
// bottleneck breaks it: every agent funnels through the one crossing row, then
// walks the goal column to find its own tile, straight through teammates who
// arrived earlier and will never move again. `SettledTag` is what keeps the
// layout honest once that happens — see `Traveler` above.
constexpr auto home_tile(std::size_t i) -> tess::Coord3 {
  return {9 - static_cast<std::int64_t>(i / kHeight),
          static_cast<std::int64_t>(i % kHeight), 0};
}
constexpr auto away_tile(std::size_t i) -> tess::Coord3 {
  return {kWidth - 3 - static_cast<std::int64_t>(i / kHeight),
          static_cast<std::int64_t>(i % kHeight), 0};
}

struct Demo {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathRequestRuntime runtime;
  tess::PathAgentTickState tick_state;
  tess::LocalTopologyScratch topo_scratch;
  tess::PathScratch settle_scratch;
  tess::RegionGraphScratch graph_scratch;
  tess::RegionGraph graph;
  tess::FrameOps ops;
  tess::DeltaCollector deltas;
  std::vector<tess::Coord3> pending_walls;
  std::vector<tess::ChunkKey> dirty_scratch;
  std::vector<std::uint8_t> shadow;    // 0 open, 1 wall, per tile.
  std::vector<std::int16_t> agent_xy;  // x,y pairs for JS.
  tess::RenderVersion version{};
  std::size_t built_tiles = 0;
  bool replan_each_tick = false;
  double last_tick_us = 0.0;
  // Persistent schedule clock and accumulator: the carry between frames is
  // what turns measured real deltas into a wall-clock 20 Hz simulation.
  // (tick_state owns the separate agent-tick clock; leave it alone.)
  tess::SimClock sim_clock;
  tess::FixedStepAccumulator accumulator{20, 8};
  bool outbound = true;
  int trips = 1;

  struct BuildTaskFn {
    Demo* demo;
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
        passable[local.value] = false;
        construction[local.value] = true;
        ++ack.tiles;
      }
    }
  };

  using BuildTask = tess::AutoExecTask<World, tess::WritePolicy::UniquePerChunk,
                                       BuildAck, BuildTaskFn>;
  std::unique_ptr<BuildTask> build_task;

  explicit Demo(int agent_count) {
    for (auto& page : world.chunks()) {
      auto passable = page.field_span<PassableTag>();
      auto cost = page.field_span<CostTag>();
      auto settled = page.field_span<SettledTag>();
      for (std::size_t i = 0; i < passable.size(); ++i) {
        passable[i] = true;
        cost[i] = 1;
        settled[i] = false;
      }
    }
    runtime.reserve_requests(2048);
    runtime.reserve_search_nodes(65536);
    runtime.reserve_path_nodes(262144);
    deltas.reserve(World::chunk_count, 8192, 16);
    tess::build_region_graph<World, Walker>(world, topo_scratch, graph);

    agents.resize(static_cast<std::size_t>(agent_count));
    agent_xy.resize(agents.size() * 2);
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agents[i].position = home_tile(i);
      world.field<OccupancyTag>(agents[i].position) = true;
      tess::set_path_agent_goal(tick_state, agents[i], away_tile(i));
    }

    shadow.assign(static_cast<std::size_t>(kWidth) * kHeight, 0);

    build_task = std::make_unique<BuildTask>(world, ops, BuildTaskFn{this});
    build_task->reserve_operations(64);
    build_task->set_result_hook(
        this, [](void* ctx, tess::OpHandle, const tess::OpCompletion& done,
                 const BuildAck* ack) noexcept {
          auto* self = static_cast<Demo*>(ctx);
          if (done.ok() && ack != nullptr) {
            self->built_tiles += ack->tiles;
            // AutoExec runs every accepted chunk kernel before draining any
            // hook. Duplicate UniquePerChunk operations are rejected only
            // after the first operation for that same chunk is accepted, and
            // that kernel applies every pending wall in the chunk. Clearing on
            // the first successful completion therefore cannot hide work from
            // a later kernel or discard a rejected sibling's distinct chunk.
            self->pending_walls.clear();
          }
        });

    schedule.reserve_tasks(3);
    (void)schedule.add_task(
        {"build", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
        *build_task);
    (void)schedule.add_task({"topology", tess::SimPhase::Topology,
                             tess::Cadence::on_dirty(kTerrainDirty)},
                            topology_task);
    (void)schedule.add_task(
        {"agents", tess::SimPhase::Movement, tess::Cadence::every_tick()},
        agent_task);
    schedule.seal();

    // A fresh consumer only accepts a baseline; seed the shadow grid now.
    tess::collect_baseline(deltas, world, kTerrainDirty);
    consume_frame(deltas.publish());
  }

  struct TopologyTaskFn {
    Demo* demo = nullptr;
    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      demo->dirty_scratch.clear();
      demo->world.collect_dirty_chunks(kTerrainDirty, demo->dirty_scratch);
      if (!demo->dirty_scratch.empty()) {
        tess::update_region_graph<World, Walker>(
            demo->world, demo->topo_scratch, demo->graph, demo->dirty_scratch);
        tess::mark_pathing_dirty(demo->tick_state);
      }
      return {};
    }
  };

  // Runs before planning, so routes are always built against current facts.
  //
  // Two jobs. First, republish which tiles are settled: an agent that has
  // arrived, or that the library gave up on, is a permanent obstacle, and
  // `Traveler` routes around it. Nothing else marks these -- occupancy cannot,
  // since travelling agents set it too.
  //
  // Second, keep the terminal verdict honest. The library's retry budget is a
  // clock: it cannot tell a jammed queue from a sealed goal, and a bottleneck
  // makes long jams ordinary. So decide the verdict by asking, in two stages
  // ordered by cost.
  //
  // The terrain graph answers the cheap question. It is stamped for `Walker`,
  // so `precheck_path<Walker>` is valid against it, and `Traveler` only ever
  // subtracts tiles from `Walker` -- terrain that seals a goal seals it for
  // the agent too. Settling that here, on the first blocked tick, is what
  // keeps a sealed colony affordable: an agent left in `Blocked`/`NoPath` is
  // resubmitted every tick, and 1,024 of them re-searching the whole region
  // every tick is a multi-second freeze on the page.
  //
  // Only when terrain says a route exists is the expensive question worth
  // asking: is there still one with the settled colonists in the way? If yes
  // the agent is queueing, and its budget is refunded -- it will move as soon
  // as the queue ahead does. If no, it keeps its retries and goes terminal on
  // the clock, which is the one case where the clock is the right answer:
  // being boxed in by teammates is real, but it can clear when they relaunch.
  void refresh_settled_agents() {
    for (auto& agent : agents) {
      const auto settled =
          !agent.has_goal || agent.phase == tess::PathAgentPhase::Unreachable;
      world.field<SettledTag>(agent.position) = settled;
    }
    for (auto& agent : agents) {
      if (agent.phase != tess::PathAgentPhase::Blocked || !agent.has_goal) {
        continue;
      }
      if (tess::precheck_path<Walker>(graph, world, agent.position, agent.goal,
                                      graph_scratch) ==
          tess::PrecheckStatus::Unreachable) {
        agent.phase = tess::PathAgentPhase::Unreachable;
        agent.status = tess::PathStatus::NoPath;
        continue;
      }
      if (agent.blocked_retries < kMaxBlockedRetries / 2) {
        continue;
      }
      // The agent stands on its own tile and a search rejects an impassable
      // start, so lift its own marker for the probe. Only a settled agent
      // marks itself, and a Blocked agent is not settled -- but a terminal
      // agent re-probed after a wall change would be, so clear it either way.
      const auto marked = world.field<SettledTag>(agent.position);
      world.field<SettledTag>(agent.position) = false;
      const auto route = tess::weighted_astar_path<World, Traveler>(
          world, tess::PathRequest{agent.position, agent.goal}, settle_scratch);
      world.field<SettledTag>(agent.position) = marked;
      if (route.status == tess::PathStatus::Found) {
        agent.blocked_retries = 0;
      }
    }
  }

  struct AgentTaskFn {
    Demo* demo = nullptr;
    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      demo->refresh_settled_agents();
      // Marked here, not in tick(): a frame may grant several fixed ticks,
      // and the toggle promises a replan on every one of them.
      if (demo->replan_each_tick) {
        tess::mark_pathing_dirty(demo->tick_state);
      }
      auto options = tess::PathAgentTickOptions{};
      options.max_blocked_retries = kMaxBlockedRetries;
      // No graph here, deliberately. The graph models terrain and is stamped
      // for `Walker`; `precheck_path` rejects a stamp mismatch outright
      // (GraphStale), so handing it to a `Traveler` search would never prune
      // anything and would only read as though it did. Keeping the graph on
      // terrain is still right -- rebuilding it as colonists settle would
      // churn topology over something that is not terrain -- so the precheck
      // it can soundly answer is made explicitly, in refresh_settled_agents.
      (void)tess::tick_weighted_path_agents_with_movement<
          World, Traveler, kMaxCost, OccupancyTag, ReservationTag>(
          demo->tick_state, demo->world, demo->agents, demo->runtime, options,
          0, nullptr);
      return {};
    }
  };

  TopologyTaskFn topology_task{this};
  AgentTaskFn agent_task{this};

  // Declared after every task object it references: members are destroyed
  // in reverse declaration order, and the non-owning Schedule must go first.
  tess::Schedule schedule;

  void queue_wall(tess::Coord3 coord) {
    pending_walls.push_back(coord);
    const auto key = tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord));
    (void)ops.update_field(
        tess::DomainDesc::explicit_chunks({&key, 1}),
        tess::FieldAccessDesc{0, kTerrainDirty, kTerrainDirty},
        tess::WritePolicy::UniquePerChunk);
  }

  // Consumes one published DeltaFrame with invalidation semantics: covered
  // tiles are re-read from the current world into the shadow grid.
  void consume_frame(const tess::DeltaFrame& frame) {
    if (!tess::delta_frame_applicable(frame.header, version)) {
      return;
    }
    const auto repaint = [&](tess::Coord3 coord) {
      const auto wall = world.field<ConstructionTag>(coord);
      shadow[static_cast<std::size_t>(coord.y) * kWidth +
             static_cast<std::size_t>(coord.x)] = wall ? 1 : 0;
    };
    for (const auto& chunk : frame.chunks) {
      if (chunk.tile_count != 0) {
        for (std::uint32_t i = 0; i < chunk.tile_count; ++i) {
          repaint(frame.tiles[chunk.first_tile + i].coord);
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
  }

  // Advances the simulation by the measured real elapsed seconds. Returns
  // the average cost of one fixed tick in microseconds, or -1 when the
  // accumulator granted no tick this frame.
  auto tick(double dt_seconds) -> double {
    const auto begin = std::chrono::steady_clock::now();
    const auto summary =
        tess::run_schedule_frame(schedule, sim_clock, accumulator, dt_seconds,
                                 tess::SimTimeControl{tess::SimSpeed::Speed1x});
    tess::collect_tile_deltas(deltas, world, kTerrainDirty);
    consume_frame(deltas.publish());
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agent_xy[i * 2] = static_cast<std::int16_t>(agents[i].position.x);
      agent_xy[i * 2 + 1] = static_cast<std::int16_t>(agents[i].position.y);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_us =
        std::chrono::duration<double, std::micro>(end - begin).count();
    last_tick_us = summary.ticks > 0
                       ? elapsed_us / static_cast<double>(summary.ticks)
                       : -1.0;
    return last_tick_us;
  }

  auto arrived() const -> int {
    int count = 0;
    for (const auto& agent : agents) {
      if (!agent.has_goal) {
        ++count;
      }
    }
    return count;
  }

  auto unreachable() const -> int {
    int count = 0;
    for (const auto& agent : agents) {
      if (agent.phase == tess::PathAgentPhase::Unreachable) {
        ++count;
      }
    }
    return count;
  }

  // Flips every agent's goal to the opposite side once the whole colony has
  // arrived. On the return trip the convoy leader (highest batch) is
  // processed last within each row, so the first few ticks are a harmless
  // accordion of transient Occupied results — expected, not a bug.
  // Terminal agents deliberately keep their failed goal and do not relaunch:
  // the page reports that state and requires reset/clear-walls to change the
  // environment rather than silently retrying a proven-unreachable trip. That
  // is only a defensible policy because `refresh_settled_agents` reserves the
  // terminal verdict for agents with no route at all — a jammed queue gets its
  // retry budget back instead of being written off.
  auto relaunch() -> int {
    if (arrived() != static_cast<int>(agents.size())) {
      return trips;
    }
    outbound = !outbound;
    for (std::size_t i = 0; i < agents.size(); ++i) {
      // Leaving home clears the settled marker: the tile is a through route
      // again, not an obstacle, from the moment its owner is travelling.
      world.field<SettledTag>(agents[i].position) = false;
      tess::set_path_agent_goal(tick_state, agents[i],
                                outbound ? away_tile(i) : home_tile(i));
    }
    ++trips;
    return trips;
  }
};

std::unique_ptr<Demo> demo;

}  // namespace

extern "C" {

TESS_DEMO_EXPORT int tess_colony_width() { return kWidth; }
TESS_DEMO_EXPORT int tess_colony_height() { return kHeight; }

TESS_DEMO_EXPORT int tess_colony_reset(int agent_count) {
  if (agent_count < 1) {
    agent_count = 1;
  }
  if (agent_count > kMaxAgents) {
    agent_count = kMaxAgents;
  }
  demo = std::make_unique<Demo>(agent_count);
  return agent_count;
}

TESS_DEMO_EXPORT int tess_colony_set_wall(int x, int y) {
  if (!demo || x < kWallMinX || x > kWallMaxX || y < 0 || y >= kHeight) {
    return 0;
  }
  demo->queue_wall(tess::Coord3{x, y, 0});
  return 1;
}

TESS_DEMO_EXPORT void tess_colony_set_strategy(int replan_each_tick) {
  if (demo) {
    demo->replan_each_tick = replan_each_tick != 0;
  }
}

TESS_DEMO_EXPORT double tess_colony_tick(double dt_seconds) {
  return demo ? demo->tick(dt_seconds) : -1.0;
}

TESS_DEMO_EXPORT int tess_colony_relaunch() {
  return demo ? demo->relaunch() : 0;
}

TESS_DEMO_EXPORT const std::uint8_t* tess_colony_tiles() {
  return demo ? demo->shadow.data() : nullptr;
}

TESS_DEMO_EXPORT const std::int16_t* tess_colony_agents() {
  return demo ? demo->agent_xy.data() : nullptr;
}

TESS_DEMO_EXPORT int tess_colony_agent_count() {
  return demo ? static_cast<int>(demo->agents.size()) : 0;
}

TESS_DEMO_EXPORT int tess_colony_arrived() {
  return demo ? demo->arrived() : 0;
}

TESS_DEMO_EXPORT int tess_colony_unreachable() {
  return demo ? demo->unreachable() : 0;
}

}  // extern "C"

int main() {
#ifndef __EMSCRIPTEN__
  // The browser entry points retain their established exception behavior.
  // The native executable is a self-check, so convert setup/allocation
  // failures into a diagnostic instead of escaping main and terminating.
  try {
#endif
    tess_colony_reset(8);
#ifndef __EMSCRIPTEN__
    for (int frame = 0; frame < 5000 && tess_colony_arrived() < 8; ++frame) {
      if (frame == 4) {
        for (int y = 0; y < kHeight - 8; ++y) {
          tess_colony_set_wall(64, y);
        }
      }
      (void)tess_colony_tick(0.05);
    }
    if (tess_colony_arrived() != 8) {
      std::cerr << "web colony model: agents did not arrive\n";
      return 1;
    }
    if (demo->built_tiles != static_cast<std::size_t>(kHeight - 8)) {
      std::cerr << "web colony model: wall not built\n";
      return 1;
    }
    const auto* tiles = tess_colony_tiles();
    if (tiles[64 + 0 * kWidth] != 1 ||
        tiles[64 + (kHeight - 1) * kWidth] != 0) {
      std::cerr << "web colony model: shadow grid mismatch\n";
      return 1;
    }

    // Regression: a bottleneck must not wedge the colony. Two wall segments
    // that overlap in y but stand apart in x leave one open channel, so every
    // agent has to funnel through a single crossing row and then walk the goal
    // column past teammates who arrived before it. Planning that ignored those
    // settled teammates deadlocked the whole convoy behind the first one, and
    // the retry budget then reported a full half of the colony as terminal
    // even though each still had a clear route to its goal.
    constexpr int kBottleneckAgents = 128;
    tess_colony_reset(kBottleneckAgents);
    for (int frame = 0; frame < 400; ++frame) {
      (void)tess_colony_tick(0.05);
      if (tess_colony_arrived() == kBottleneckAgents) {
        (void)tess_colony_relaunch();
      }
    }
    for (int y = 0; y <= 74; ++y) {
      tess_colony_set_wall(60, y);
    }
    for (int y = 63; y < kHeight; ++y) {
      tess_colony_set_wall(48, y);
    }
    int completed_trips = 0;
    for (int frame = 0; frame < 4000 && completed_trips < 2; ++frame) {
      (void)tess_colony_tick(0.05);
      if (tess_colony_arrived() == kBottleneckAgents) {
        (void)tess_colony_relaunch();
        ++completed_trips;
      }
    }
    if (tess_colony_unreachable() != 0) {
      std::cerr << "web colony model: " << tess_colony_unreachable()
                << " agents wedged behind the bottleneck\n";
      return 1;
    }
    if (completed_trips < 2) {
      std::cerr << "web colony model: convoy stalled at the bottleneck ("
                << tess_colony_arrived() << "/" << kBottleneckAgents
                << " arrived)\n";
      return 1;
    }

    // The other half of that contract: refusing to cry wolf must not cost the
    // demo its ability to report a real seal. A wall spanning every row leaves
    // no route at all, and the page is expected to say so.
    tess_colony_reset(8);
    for (int y = 0; y < kHeight; ++y) {
      tess_colony_set_wall(64, y);
    }
    for (int frame = 0; frame < 300; ++frame) {
      (void)tess_colony_tick(0.05);
    }
    if (tess_colony_unreachable() != 8) {
      std::cerr << "web colony model: sealed goals not reported as terminal ("
                << tess_colony_unreachable() << "/8)\n";
      return 1;
    }
    std::cout << "web colony model: ok\n";
  } catch (const std::exception& error) {
    std::cerr << "web colony model: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "web colony model: unknown failure\n";
    return 1;
  }
#endif
  return 0;
}

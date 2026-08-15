// Browser build of the colony_2d composition: queued wall construction, an
// OnDirty topology rebuild, movement-class agents, and a DeltaFrame-driven
// shadow grid, exported to JavaScript. Compiled natively it self-checks the
// same model (see main below); compiled with Emscripten it becomes the
// /demo/colony/ interactive page.

#include <tess/core/config.h>
#include <tess/pathfinding.h>
#include <tess/simulation.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_DEMO_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_DEMO_EXPORT
#endif

#include <algorithm>
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
// Set on the tile of every agent quiescent for the current leg -- one that has
// arrived, is crowd-blocked until turnaround, or is durably unreachable.
// Distinct from occupancy, which a travelling agent also sets and vacates a
// tick later.
struct SettledTag {};

constexpr int kWidth = 128;
constexpr int kHeight = 128;
constexpr int kMaxAgents = 1024;
// Wall painting is rejected outside this band so the spawn columns on the
// left and the turnaround columns on the right always stay standable.
constexpr int kWallMinX = 10;
constexpr int kWallMaxX = kWidth - 11;
// Recovery probes begin after half this window. It only has to be long enough
// that ordinary convoy shuffling does not pay for an exact reachability probe.
constexpr std::uint32_t kRecoveryWindowTicks = 32;
constexpr std::size_t kMaxPlanningQueriesPerTick = 8;
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
// bottleneck makes that routine: see the regression scenario in main).
using Traveler = tess::movement::MovementClass<
    tess::movement::AllOf<
        tess::movement::Field<PassableTag>,
        tess::movement::Not<tess::movement::Field<ConstructionTag>>,
        tess::movement::Not<tess::movement::Field<SettledTag>>>,
    tess::movement::FieldCost<CostTag>>;
constexpr std::uint32_t kMaxCost = 4;
constexpr std::uint32_t kTerrainDirty = 1U << 0U;
// Bumps a chunk's content version when a colonist settles or leaves, without
// waking the terrain consumers. Deliberately not kTerrainDirty: the topology
// task and the delta collector both filter on that bit, and a colonist parking
// is not terrain. See publish_settled_agents for why the version has to move.
constexpr std::uint32_t kSettledDirty = 1U << 1U;

struct BuildAck {
  std::size_t tiles = 0;
};

// Convoy layout: batch k = i / kHeight walks row y = i % kHeight between
// column 9 - k (home) and kWidth - 3 - k (away). Every agent owns a distinct
// goal tile — occupancy admits exactly one occupant, so shared goals would
// leave all but one agent blocked forever — and every leg has equal length,
// with the outbound leader (k = 0) starting ahead of its followers so nobody
// parks in front of a teammate still travelling.
//
// That last clause holds only while each agent keeps to its own row. A painted
// bottleneck breaks it: every agent funnels through the one crossing row, then
// walks the goal column to find its own tile, straight through teammates who
// arrived earlier and remain still for the leg. `SettledTag` keeps planning
// honest, while the wave controller turns everyone around together if that
// temporary packing blocks a remaining goal.
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
  tess::BlockedAgentRecoverySchedule recovery_schedule;
  tess::BlockedAgentRecoveryStats recovery_stats;
  tess::PathAgentReplanQueue replan_queue;
  tess::PathAgentFrameStats replan_stats;
  tess::LocalTopologyScratch topo_scratch;
  tess::PathScratch settle_scratch;
  tess::PathScratch replan_scratch;
  tess::JointMoveScratch joint_scratch;
  tess::RegionGraphScratch graph_scratch;
  tess::RegionGraph graph;
  // False once a region-graph UPDATE reports a status other than Built.
  // update_region_graph can return InvalidChunk for an out-of-range dirty
  // chunk; build_region_graph has no reachable failure, so it is not
  // checked. The demo cannot recover, so it stops replanning rather than
  // route against connectivity it knows is stale.
  bool topology_ok = true;
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
  std::vector<std::uint8_t> crowd_blocked;
  std::vector<std::uint8_t> terrain_confirmation_pending;
  bool outbound = true;
  int leg = 1;
  int completed_legs = 0;
  int aborted_legs = 0;
  // Consecutive fixed ticks with zero agent movement. See AgentTaskFn.
  std::size_t stalled_ticks = 0;
  std::size_t max_recovery_probes = 0;
  std::size_t max_planning_queries = 0;

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
    joint_scratch.reserve(static_cast<std::size_t>(agent_count));
    recovery_schedule.reserve(static_cast<std::size_t>(agent_count));
    replan_queue.reserve(static_cast<std::size_t>(agent_count));
    runtime.reserve_requests(2048);
    runtime.reserve_search_nodes(65536);
    runtime.reserve_path_nodes(262144);
    replan_scratch.reserve_nodes(65536);
    deltas.reserve(World::chunk_count, 8192, 16);
    tess::build_region_graph<World, Walker>(world, topo_scratch, graph);

    agents.resize(static_cast<std::size_t>(agent_count));
    agent_xy.resize(agents.size() * 2);
    crowd_blocked.assign(agents.size(), 0);
    terrain_confirmation_pending.assign(agents.size(), 0);
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agents[i].position = home_tile(i);
      world.field<OccupancyTag>(agents[i].position) = true;
      tess::set_path_agent_goal(tick_state, agents[i], away_tile(i));
      agents[i].phase = tess::PathAgentPhase::Blocked;
    }
    replan_queue.request_all(agents);
    tick_state.pathing_dirty = false;

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
    (void)schedule.add_task({"topology", tess::SimPhase::Pathing,
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
        // Only mark pathing dirty if the graph actually refreshed. Replanning
        // against a graph whose update failed would route agents using stale
        // connectivity, which is harder to notice than not replanning at all.
        const auto updated = tess::update_region_graph<World, Walker>(
            demo->world, demo->topo_scratch, demo->graph, demo->dirty_scratch);
        demo->topology_ok =
            demo->topology_ok && updated.status == tess::TopologyStatus::Built;
        if (demo->topology_ok) {
          demo->replan_queue.request_all(demo->agents);
        }
      }
      return {};
    }
  };

  // Runs before planning, so routes are always built against current facts.
  //
  // Two jobs. First, republish which tiles are settled for this leg: an agent
  // that arrived or reached either quiescent outcome is an obstacle until the
  // next wave, and `Traveler` routes around it. Nothing else marks these --
  // occupancy cannot, since travelling agents set it too.
  //
  // Second, keep the durable failure verdict honest. A retry clock cannot
  // distinguish a jammed queue from a sealed goal, so exhausted agents stay
  // Blocked. The library recovery schedule selects only a bounded,
  // jittered subset for the staged verdict below.
  //
  // The terrain graph answers the cheap question. It is stamped for `Walker`,
  // so `precheck_path<Walker>` is valid against it, and `Traveler` only ever
  // subtracts tiles from `Walker` -- terrain that seals a goal seals it for
  // the agent too. Settling that before exact search keeps a sealed colony
  // affordable.
  //
  // Only when terrain says a route exists is the expensive question worth
  // asking: is there still one with the settled colonists in the way? The
  // demo's costs are immutable and uniformly one, so unit A* answers that
  // reachability question without paying for a weighted route. A second exact
  // Walker search distinguishes durable terrain failure from a route blocked
  // only by settled teammates; snapshot NoPath alone is not a durable claim.
  void publish_settled_agents() {
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
      // Mark then clear: the version bump is the part the cache fingerprint
      // reads, and clearing leaves no dirty flag for the terrain consumers.
      // Only on an actual change, or every tick would invalidate the cache.
      //
      // Currently inert here: this demo plans through the weighted batch, and
      // the route cache is consulted only by the unit path. It is done anyway
      // because the obligation attaches to editing a field the movement class
      // reads, not to today's choice of planner -- switching this demo to unit
      // planning, or enabling the field-product caches, would otherwise
      // reintroduce the bug silently. tests/tess_path_runtime_test.cc pins the
      // behaviour on the path where it does bite.
      const auto key =
          tess::chunk_key<Shape>(tess::chunk_coord<Shape>(agent.position));
      world.mark_dirty(key, kSettledDirty,
                       tess::Box3{agent.position, tess::Extent3{1, 1, 1}});
      world.clear_dirty(key, kSettledDirty);
    }
  }

  auto recover_blocked_agents(std::uint64_t tick, std::size_t max_queries)
      -> std::size_t {
    auto recovery_options = kRecoveryOptions;
    recovery_options.max_probes_per_tick = max_queries;
    recovery_stats =
        recovery_schedule.collect_due(agents, tick, recovery_options);
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
    Demo* demo = nullptr;
    auto operator()(const tess::ScheduleTaskContext& context)
        -> tess::ScheduleTaskResult {
      demo->publish_settled_agents();
      // Marked here, not in tick(): a frame may grant several fixed ticks,
      // and the toggle promises a replan on every one of them.
      if (demo->replan_each_tick) {
        // The synchronous diagnostic strategy already replans every active
        // agent. It still needs the bounded classifier: replanning alone
        // cannot distinguish a wall seal from settled-only snapshot NoPath.
        demo->replan_queue.clear();
        demo->replan_stats = {};
        (void)demo->recover_blocked_agents(context.clock.tick,
                                           kMaxPlanningQueriesPerTick);
        // Found recovery results need no retained-route work because the
        // synchronous pass below immediately replaces every active route.
        demo->replan_queue.clear();
        tess::mark_pathing_dirty(demo->tick_state);
      } else {
        demo->replan_stats =
            tess::process_weighted_path_agent_replans<World, Traveler>(
                demo->world, demo->agents, demo->tick_state.routes,
                demo->replan_queue, demo->replan_scratch,
                tess::PathAgentReplanOptions{
                    .max_requests = kMaxPlanningQueriesPerTick,
                });
        const auto remaining_queries =
            kMaxPlanningQueriesPerTick - demo->replan_stats.submitted;
        const auto recovery_queries =
            demo->recover_blocked_agents(context.clock.tick, remaining_queries);
        demo->max_planning_queries =
            std::max(demo->max_planning_queries,
                     demo->replan_stats.submitted + recovery_queries);
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
      // Joint movement with SwapPolicy::Permit: a mutually blocked pair
      // exchanges tiles instead of wedging. The library default is Forbid --
      // the standard MAPF constraint -- but this demo's colonists have no
      // physical extent and its recorded failure class (three-wall layouts
      // deadlocking two travellers head-on, found by randomised search) is
      // exactly the 2-cycle the exchange resolves. Chains and rotations are
      // admitted under any policy, so convoys also drain in one tick.
      const auto stats = tess::tick_weighted_path_agents_with_joint_movement<
          World, Traveler, kMaxCost, OccupancyTag, ReservationTag>(
          demo->tick_state, demo->world, demo->agents, demo->runtime,
          demo->joint_scratch, options,
          tess::JointMoveOptions{tess::SwapPolicy::Permit}, 0, nullptr);
      // A colony can stop dead without any agent being durably blocked --
      // two agents each standing on the tile the other needs will block, be
      // refunded, and block again forever. Nobody reports that today, so the
      // page reads "Colony running" over a frozen grid. Count consecutive
      // ticks in which not one agent moved; the page turns that into a
      // stalled message. Arrival is progress, so a colony waiting out the
      // relaunch dwell is not stalled.
      if (stats.movement.advanced == 0 && demo->active() > 0) {
        ++demo->stalled_ticks;
      } else {
        demo->stalled_ticks = 0;
      }
      return {};
    }
  };

  TopologyTaskFn topology_task{this};
  AgentTaskFn agent_task{this};

  // Declared after every task object it references: members are destroyed
  // in reverse declaration order, and the non-owning Schedule must go first.
  tess::Schedule schedule;

  [[nodiscard]] auto queue_wall(tess::Coord3 coord) -> bool {
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

  auto active() const -> int {
    int count = 0;
    for (const auto& agent : agents) {
      if (agent.has_goal && agent.phase != tess::PathAgentPhase::Unreachable) {
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
  return demo->queue_wall(tess::Coord3{x, y, 0}) ? 1 : 0;
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

TESS_DEMO_EXPORT int tess_colony_leg() {
  return demo ? demo->current_leg() : 0;
}

TESS_DEMO_EXPORT int tess_colony_completed_legs() {
  return demo ? demo->completed_leg_count() : 0;
}

TESS_DEMO_EXPORT int tess_colony_aborted_legs() {
  return demo ? demo->aborted_leg_count() : 0;
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

TESS_DEMO_EXPORT int tess_colony_crowd_blocked() {
  return demo ? demo->crowd_blocked_count() : 0;
}

TESS_DEMO_EXPORT int tess_colony_turnaround_ready() {
  return demo && demo->turnaround_ready() ? 1 : 0;
}

/// Consecutive fixed ticks in which no agent moved while goals remain.
TESS_DEMO_EXPORT int tess_colony_stalled_ticks() {
  return demo ? static_cast<int>(demo->stalled_ticks) : 0;
}

}  // extern "C"

int main() {
#ifndef __EMSCRIPTEN__
  // The browser entry points retain their established exception behavior.
  // The native executable is a self-check, so convert setup/allocation
  // failures into a diagnostic instead of escaping main and terminating.
#if TESS_HAS_EXCEPTIONS
  try {
#endif
    // A browser wall request is an admission decision, not permission to
    // corrupt the movement state. Painting the occupied tile under a moving
    // agent must be rejected synchronously; after the agent vacates it, the
    // same tile is a valid construction target.
    tess_colony_reset(1);
    (void)tess_colony_tick(0.05);
    const auto occupied_tile = demo->agents[0].position;
    if (tess_colony_set_wall(static_cast<int>(occupied_tile.x),
                             static_cast<int>(occupied_tile.y)) != 0) {
      std::cerr << "web colony model: occupied wall request was accepted\n";
      return 1;
    }
    (void)tess_colony_tick(0.25);
    if (!demo->world.field<PassableTag>(occupied_tile) ||
        demo->world.field<ConstructionTag>(occupied_tile)) {
      std::cerr << "web colony model: rejected wall changed the world\n";
      return 1;
    }
    if (tess_colony_set_wall(static_cast<int>(occupied_tile.x),
                             static_cast<int>(occupied_tile.y)) != 1) {
      std::cerr << "web colony model: vacated wall request was rejected\n";
      return 1;
    }
    (void)tess_colony_tick(0.05);
    if (demo->world.field<PassableTag>(occupied_tile) ||
        !demo->world.field<ConstructionTag>(occupied_tile)) {
      std::cerr << "web colony model: accepted wall was not built\n";
      return 1;
    }

    // Low-level invariant check: if a caller bypasses wall admission and makes
    // an occupied source impassable, exact pathing still rejects InvalidStart.
    // That library contract is distinct from the demo policy preventing the
    // invalid state in the first place.
    tess_colony_reset(1);
    auto& invalid_start_agent = demo->agents[0];
    demo->world.field<PassableTag>(invalid_start_agent.position) = false;
    invalid_start_agent.phase = tess::PathAgentPhase::Blocked;
    invalid_start_agent.status = tess::PathStatus::Found;
    demo->recover_blocked_agents(1, 1);
    const auto invalid_start_first_queries =
        demo->recover_blocked_agents(1 + kRecoveryWindowTicks, 1);
    if (invalid_start_first_queries != 1 ||
        demo->terrain_confirmation_pending[0] == 0) {
      std::cerr << "web colony model: InvalidStart confirmation not deferred\n";
      return 1;
    }
    const auto invalid_start_second_queries =
        demo->recover_blocked_agents(2 + kRecoveryWindowTicks, 1);
    if (invalid_start_agent.phase != tess::PathAgentPhase::Unreachable ||
        invalid_start_agent.status != tess::PathStatus::InvalidStart ||
        invalid_start_second_queries != 1 ||
        demo->terrain_confirmation_pending[0] != 0) {
      std::cerr << "web colony model: invalid start retried indefinitely\n";
      return 1;
    }

    // Regression: four completed agents can encircle an unoccupied goal.
    // Terrain still has a route, so a Traveler-only NoPath is temporary -- it
    // is evidence that settled teammates must eventually wake, not permission
    // to mark the remaining agent durably blocked.
    tess_colony_reset(5);
    for (auto& agent : demo->agents) {
      demo->world.field<OccupancyTag>(agent.position) = false;
      tess::clear_path_agent_goal(agent);
    }
    tess::diagnostics::FlowAccounting enclosure_flow;
    demo->tick_state.flow_accounting = &enclosure_flow;
    constexpr auto enclosed_goal = tess::Coord3{64, 64, 0};
    constexpr tess::Coord3 positions[] = {
        {62, 64, 0}, {63, 64, 0}, {65, 64, 0}, {64, 63, 0}, {64, 65, 0}};
    for (std::size_t i = 0; i < demo->agents.size(); ++i) {
      demo->agents[i].position = positions[i];
      demo->world.field<OccupancyTag>(positions[i]) = true;
      if (i == 0) {
        tess::set_path_agent_goal(demo->tick_state, demo->agents[i],
                                  enclosed_goal);
        demo->agents[i].phase = tess::PathAgentPhase::Blocked;
        demo->agents[i].status = tess::PathStatus::Found;
      }
    }
    demo->publish_settled_agents();
    demo->recover_blocked_agents(1, 1);
    const auto enclosure_first_queries =
        demo->recover_blocked_agents(1 + kRecoveryWindowTicks, 1);
    if (enclosure_first_queries != 1 ||
        demo->terrain_confirmation_pending[0] == 0) {
      std::cerr << "web colony model: enclosure confirmation not deferred\n";
      return 1;
    }
    const auto enclosure_second_queries =
        demo->recover_blocked_agents(2 + kRecoveryWindowTicks, 1);
    if (demo->unreachable() != 0 || demo->crowd_blocked_count() != 1 ||
        !demo->turnaround_ready() || enclosure_second_queries != 1 ||
        demo->terrain_confirmation_pending[0] != 0 ||
        !enclosure_flow.counters.retention_identity_holds() ||
        enclosure_flow.counters.cancelled != 1 ||
        enclosure_flow.counters.outstanding_current != 0) {
      std::cerr << "web colony model: settled enclosure misclassified\n";
      return 1;
    }
    if (demo->relaunch() != 2 || demo->completed_leg_count() != 0 ||
        demo->aborted_leg_count() != 1 ||
        !enclosure_flow.counters.retention_identity_holds() ||
        enclosure_flow.counters.outstanding_current != 5) {
      std::cerr << "web colony model: settled wave did not turn around\n";
      return 1;
    }
    if (!demo->recovery_schedule.due_agent_indices().empty()) {
      std::cerr << "web colony model: turnaround retained recovery state\n";
      return 1;
    }
    for (int frame = 0; frame < 1000 && !demo->turnaround_ready(); ++frame) {
      (void)tess_colony_tick(0.05);
    }
    if (demo->arrived() != 5 || demo->crowd_blocked_count() != 0 ||
        demo->unreachable() != 0 ||
        !enclosure_flow.counters.retention_identity_holds() ||
        enclosure_flow.counters.completed != 5 ||
        enclosure_flow.counters.outstanding_current != 0) {
      std::cerr << "web colony model: post-enclosure leg did not complete\n";
      return 1;
    }
    if (demo->relaunch() != 3 || demo->completed_leg_count() != 1 ||
        demo->aborted_leg_count() != 1 ||
        !enclosure_flow.counters.retention_identity_holds() ||
        enclosure_flow.counters.outstanding_current != 5) {
      std::cerr << "web colony model: post-enclosure accounting diverged\n";
      return 1;
    }

    // The all-agent-replan comparison mode uses the same demo-owned outcome
    // classifier. Exercise its reachable-terrain path, not only a graph-pruned
    // wall seal.
    tess_colony_reset(5);
    for (auto& agent : demo->agents) {
      demo->world.field<OccupancyTag>(agent.position) = false;
      tess::clear_path_agent_goal(agent);
    }
    for (std::size_t i = 0; i < demo->agents.size(); ++i) {
      demo->agents[i].position = positions[i];
      demo->world.field<OccupancyTag>(positions[i]) = true;
      if (i == 0) {
        tess::set_path_agent_goal(demo->tick_state, demo->agents[i],
                                  enclosed_goal);
        demo->agents[i].phase = tess::PathAgentPhase::Blocked;
        demo->agents[i].status = tess::PathStatus::Found;
      }
    }
    tess_colony_set_strategy(1);
    for (int frame = 0; frame < 1000 && !demo->turnaround_ready(); ++frame) {
      (void)tess_colony_tick(0.05);
    }
    if (demo->arrived() != 4 || demo->crowd_blocked_count() != 1 ||
        demo->unreachable() != 0 || demo->relaunch() != 2) {
      std::cerr << "web colony model: replan mode misclassified enclosure\n";
      return 1;
    }
    for (int frame = 0; frame < 1000 && !demo->turnaround_ready(); ++frame) {
      (void)tess_colony_tick(0.05);
    }
    if (demo->arrived() != 5 || demo->crowd_blocked_count() != 0 ||
        demo->unreachable() != 0) {
      std::cerr << "web colony model: replan enclosure recovery failed\n";
      return 1;
    }

    tess_colony_reset(8);
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
    // the retry budget then reported a full half of the colony as durable
    // even though each still had a clear route to its goal.
    constexpr int kBottleneckAgents = 128;
    tess_colony_reset(kBottleneckAgents);
    for (int frame = 0; frame < 400; ++frame) {
      (void)tess_colony_tick(0.05);
      if (demo->turnaround_ready()) {
        (void)demo->relaunch();
      }
    }
    for (int y = 0; y <= 74; ++y) {
      tess_colony_set_wall(60, y);
    }
    for (int y = 63; y < kHeight; ++y) {
      tess_colony_set_wall(48, y);
    }
    const auto bottleneck_start_leg = demo->current_leg();
    const auto bottleneck_start_completed = demo->completed_leg_count();
    for (int frame = 0;
         frame < 4000 && demo->current_leg() < bottleneck_start_leg + 2;
         ++frame) {
      (void)tess_colony_tick(0.05);
      if (demo->turnaround_ready()) {
        (void)demo->relaunch();
      }
    }
    if (tess_colony_unreachable() != 0) {
      std::cerr << "web colony model: " << tess_colony_unreachable()
                << " agents wedged behind the bottleneck\n";
      return 1;
    }
    if (demo->current_leg() < bottleneck_start_leg + 2 ||
        demo->completed_leg_count() <= bottleneck_start_completed) {
      std::cerr << "web colony model: convoy stalled at the bottleneck ("
                << tess_colony_arrived() << "/" << kBottleneckAgents
                << " arrived)\n";
      return 1;
    }
    if (demo->max_recovery_probes > kRecoveryOptions.max_probes_per_tick) {
      std::cerr << "web colony model: recovery probe budget exceeded ("
                << demo->max_recovery_probes << "/"
                << kRecoveryOptions.max_probes_per_tick << ")\n";
      return 1;
    }
    if (demo->max_planning_queries > kMaxPlanningQueriesPerTick) {
      std::cerr << "web colony model: shared planning budget exceeded ("
                << demo->max_planning_queries << "/"
                << kMaxPlanningQueriesPerTick << ")\n";
      return 1;
    }

    // All eight destination columns remain a normal successful case when no
    // obstacle disturbs their equal-length routes. Exercise both directions
    // at maximum population before injecting the reported partial outcome.
    tess_colony_reset(kMaxAgents);
    for (int frame = 0; frame < 1200 && demo->current_leg() < 3; ++frame) {
      (void)tess_colony_tick(0.05);
      if (demo->turnaround_ready()) {
        (void)demo->relaunch();
      }
    }
    if (demo->current_leg() < 3 || demo->completed_leg_count() != 2 ||
        demo->aborted_leg_count() != 0 || demo->unreachable() != 0 ||
        demo->crowd_blocked_count() != 0) {
      std::cerr << "web colony model: open maximum-scale waves failed\n";
      return 1;
    }

    // Natural maximum-scale reproduction: a wall just before the eight goal
    // columns funnels every upper-row agent through one gap. The nearest goal
    // column settles first and cuts off farther columns, producing a
    // settled-only quiescent outcome without synthetic phase edits.
    tess_colony_reset(kMaxAgents);
    for (int frame = 0; frame < 5000 && !demo->turnaround_ready(); ++frame) {
      if (frame == 4) {
        for (int y = 0; y < 96; ++y) {
          tess_colony_set_wall(kWallMaxX, y);
        }
      }
      (void)tess_colony_tick(0.05);
    }
    if (!demo->turnaround_ready() || demo->arrived() == 0 ||
        demo->crowd_blocked_count() == 0 || demo->unreachable() != 0 ||
        demo->arrived() + demo->crowd_blocked_count() != kMaxAgents ||
        demo->relaunch() != 2 || demo->completed_leg_count() != 0 ||
        demo->aborted_leg_count() != 1) {
      std::cerr << "web colony model: natural settled seal not recovered\n";
      return 1;
    }
    for (int frame = 0; frame < 5000 && !demo->turnaround_ready(); ++frame) {
      (void)tess_colony_tick(0.05);
    }
    if (!demo->turnaround_ready() || demo->arrived() == 0 ||
        demo->crowd_blocked_count() == 0 || demo->unreachable() != 0 ||
        demo->arrived() + demo->crowd_blocked_count() != kMaxAgents ||
        demo->relaunch() != 3 || demo->completed_leg_count() != 0 ||
        demo->aborted_leg_count() != 2 || demo->active() != kMaxAgents) {
      std::cerr << "web colony model: repeated settled seal stopped the wave\n";
      return 1;
    }

    // Maximum-scale form of the reported state: 968 agents reached their away
    // tiles and 56 were crowd-blocked for this leg. The controller must treat
    // that as a quiescent wave, not 56 durable failures, and rearm all 1,024
    // toward home through the same bounded FIFO.
    tess_colony_reset(kMaxAgents);
    constexpr std::size_t kReportedArrivals = 968;
    for (auto& agent : demo->agents) {
      demo->world.field<OccupancyTag>(agent.position) = false;
    }
    for (std::size_t i = 0; i < demo->agents.size(); ++i) {
      auto& agent = demo->agents[i];
      if (i < kReportedArrivals) {
        agent.position = away_tile(i);
        tess::clear_path_agent_goal(agent);
      } else {
        agent.position = {64, static_cast<std::int64_t>(i - kReportedArrivals),
                          0};
        tess::clear_path_agent_goal(agent);
        demo->crowd_blocked[i] = 1;
      }
      demo->world.field<OccupancyTag>(agent.position) = true;
    }
    if (!demo->turnaround_ready() || demo->arrived() != kReportedArrivals ||
        demo->unreachable() != 0 ||
        demo->crowd_blocked_count() !=
            kMaxAgents - static_cast<int>(kReportedArrivals) ||
        demo->relaunch() != 2 || demo->active() != kMaxAgents ||
        demo->completed_leg_count() != 0 || demo->aborted_leg_count() != 1) {
      std::cerr << "web colony model: maximum-scale wave did not rearm\n";
      return 1;
    }
    const auto first_crowd_blocked = kReportedArrivals;
    const auto before_turnaround = demo->agents[first_crowd_blocked].position;
    for (int frame = 0;
         frame < 300 &&
         demo->agents[first_crowd_blocked].position == before_turnaround;
         ++frame) {
      (void)tess_colony_tick(0.25);
    }
    if (demo->agents[first_crowd_blocked].position == before_turnaround ||
        demo->max_planning_queries > kMaxPlanningQueriesPerTick) {
      std::cerr
          << "web colony model: maximum-scale turnaround made no progress\n";
      return 1;
    }
    for (int frame = 0; frame < 2000 && !demo->turnaround_ready(); ++frame) {
      (void)tess_colony_tick(0.05);
    }
    if (demo->arrived() != kMaxAgents || demo->crowd_blocked_count() != 0 ||
        demo->unreachable() != 0 || demo->relaunch() != 3 ||
        demo->completed_leg_count() != 1 || demo->aborted_leg_count() != 1) {
      std::cerr << "web colony model: maximum-scale recovery did not finish\n";
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
      std::cerr << "web colony model: sealed goals not reported as durable ("
                << tess_colony_unreachable() << "/8)\n";
      return 1;
    }
    if (demo->turnaround_ready() || demo->relaunch() != 1) {
      std::cerr << "web colony model: wall-sealed wave relaunched\n";
      return 1;
    }

    // The intentionally expensive comparison strategy must preserve the same
    // outcome semantics even though it discards retained routes each tick.
    tess_colony_reset(8);
    tess_colony_set_strategy(1);
    for (int y = 0; y < kHeight; ++y) {
      tess_colony_set_wall(64, y);
    }
    for (int frame = 0; frame < 300; ++frame) {
      (void)tess_colony_tick(0.05);
    }
    if (tess_colony_unreachable() != 8 || demo->turnaround_ready()) {
      std::cerr << "web colony model: replan mode misclassified wall seal\n";
      return 1;
    }

    // Regression: the three-wall geometry found by randomised search used to
    // deadlock two travelling agents head-on -- a 2-cycle nothing reported and
    // nothing could resolve -- freezing three of forty-eight agents while the
    // page read "Colony running". Under joint movement with
    // SwapPolicy::Permit that class is resolved outright, so the assertion
    // flips: the convoy must keep completing legs, nobody may be declared
    // durably blocked, and the stall counter must stay quiet. Stall reporting
    // itself remains load-bearing for unknown future wedge classes.
    constexpr int kStallAgents = 48;
    tess_colony_reset(kStallAgents);
    for (int frame = 0; frame < 300; ++frame) {
      (void)tess_colony_tick(0.05);
      if (demo->turnaround_ready()) {
        (void)demo->relaunch();
      }
    }
    constexpr int kStallWalls[3][3] = {
        {49, 47, 48}, {24, 10, 12}, {75, 23, 24}};
    for (const auto& spec : kStallWalls) {
      for (int y = 0; y < kHeight; ++y) {
        if (y < spec[1] || y >= spec[2]) {
          tess_colony_set_wall(spec[0], y);
        }
      }
    }
    const auto stall_start_leg = demo->current_leg();
    const auto stall_start_completed = demo->completed_leg_count();
    for (int frame = 0; frame < 1200; ++frame) {
      (void)tess_colony_tick(0.05);
      if (demo->turnaround_ready()) {
        (void)demo->relaunch();
      }
    }
    if (demo->current_leg() <= stall_start_leg ||
        demo->completed_leg_count() <= stall_start_completed) {
      std::cerr << "web colony model: no full leg completed through the "
                   "bottleneck geometry under joint movement\n";
      return 1;
    }
    if (tess_colony_unreachable() != 0) {
      std::cerr << "web colony model: bottleneck geometry reported "
                << tess_colony_unreachable() << " durable failures\n";
      return 1;
    }
    if (tess_colony_stalled_ticks() >= 100) {
      std::cerr << "web colony model: bottleneck geometry stalled under "
                   "joint movement ("
                << tess_colony_stalled_ticks() << " motionless ticks)\n";
      return 1;
    }
    std::cout << "web colony model: ok\n";
#if TESS_HAS_EXCEPTIONS
  } catch (const std::exception& error) {
    std::cerr << "web colony model: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "web colony model: unknown failure\n";
    return 1;
  }
#endif
#endif
  return 0;
}

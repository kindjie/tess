// Stage 3 of the budgeted-progress suite: the 60 Hz mixed colony
// (design section 7). Split into its own translation unit to keep
// each source under the repository's per-file token budget; shared
// plumbing lives in budgeted_progress_bench_common.h.

#include <span>

#include "budgeted_progress_bench_common.h"

namespace bpb_bench {

namespace {

using namespace bpb_bench;  // NOLINT(google-build-using-namespace)
namespace budgeted = tess_test::budgeted;
namespace colony = tess_test::colony;

// --- Stage 3: the 60 Hz mixed colony (design section 7) ----------------
//
// The production colony stack — schedule loop, queued churn ops with
// auto-exec, incremental region-graph topology, weighted path agents
// with movement, render deltas — driven from a paced 60 FPS frame
// loop with one wall-clock allowance per frame. The stack setup and
// per-tick body mirror tests/colony_harness.h's Colony::run() exactly,
// and the mirror is PROVEN equivalent before any budgeted cell runs:
// an unbudgeted 40-tick pass through this driver must reproduce
// Colony::run()'s final positions, step count, and arrivals on the
// reference configuration (stage-3 acceptance).
//
// Steady demand is the design's ping-pong extension: each agent owns
// a home/away endpoint pair and re-arms the opposite endpoint on the
// tick after arrival, making the cell a closed-loop system whose
// capacity axis is population; flow-stability rests on the deadline
// and age criteria (design section 7.2). The interactive navigation
// class carries the canonical admission + 32 tick deadline and a
// 64-tick settlement.
//
// Both section 7.3 views are emitted. In this stack every scheduled
// task is tick-coupled and none exposes an existing safe defer/resume
// boundary, so mixed_existing_quanta's defer-capable set is empty and
// the two views are identical by construction — the design names that
// outcome explicitly valid; the artifacts differ only in kind.

using MixedSchema =
    tess::FieldSchema<tess::Field<colony::PassableTag, bool>,
                      tess::Field<colony::CostTag, std::uint32_t>,
                      tess::Field<colony::OccupancyTag, bool>,
                      tess::Field<colony::ReservationTag, bool>>;

constexpr std::size_t kMixedAgents = 100;
// The historical stride of 8 rows seats at most 227 agents on this
// map; the population ladder tops out at 500. Stride 2 seats 908,
// and one stride for every rung keeps the ladder varying only
// population, never placement geometry.
constexpr std::int64_t kMixedPlacementStride = 2;
constexpr std::uint32_t kMixedChurnPeriod = 8;
constexpr std::uint32_t kMixedChurnChunks = 4;
constexpr std::int64_t kMixedGoalDistance = 24;
constexpr std::uint64_t kMixedAllowanceTicks = 32;
constexpr std::uint64_t kMixedSettlementTicks = 2 * kMixedAllowanceTicks;

// The whole mixed machinery is parameterized by the world shape: the
// canonical cell runs 512x512 (scale 8) and the design's capacity cell
// runs 1024x1024 (scale 16) over the same 64x64 logical map, so every
// bound, chunk key, and reservation buffer derives from the shape.
template <typename WorldShape>
struct MixedSuite {
  static constexpr std::int64_t kExtent = WorldShape::size.x;
  static constexpr std::int64_t kSuiteScale =
      kExtent / static_cast<std::int64_t>(kLogicalExtent);
  static_assert(WorldShape::size.x == WorldShape::size.y,
                "mixed cells use square worlds");
  static_assert(kExtent == kSuiteScale * kLogicalExtent,
                "world extent must be a whole multiple of the logical map");
  using MixedWorld = tess::AlwaysResidentWorld<WorldShape, MixedSchema>;
  using MixedColony = colony::Colony<WorldShape, MixedSchema>;

  struct MixedBuildFn {
    std::vector<colony::TileEdit>* pending = nullptr;
    auto operator()(auto view, colony::BuildAck& ack) -> void {
      auto passable = view.template field_span<colony::PassableTag>();
      auto cost = view.template field_span<colony::CostTag>();
      for (const auto& edit : *pending) {
        if (tess::chunk_key<WorldShape>(
                tess::chunk_coord<WorldShape>(edit.coord)) != view.key()) {
          continue;
        }
        const auto local = tess::local_tile_id<WorldShape>(
            tess::local_coord<WorldShape>(edit.coord));
        passable[local.value] = false;
        cost[local.value] = 0U;
        ++ack.tiles;
      }
    }
  };

  struct MixedStack {
    std::unique_ptr<MixedWorld> world;
    std::vector<tess::PathAgentState> agents;
    std::vector<tess::Coord3> assigned_goals;
    std::vector<tess::Coord3> homes;
    tess::PathRequestRuntime runtime;
    tess::diagnostics::FlowAccounting agent_flow;
    tess::PathAgentTickState tick_state;
    tess::LocalTopologyScratch topo_scratch;
    tess::RegionGraph graph;
    std::vector<colony::TileEdit> churn_pool;
    std::size_t churn_cursor = 0;
    std::vector<colony::TileEdit> pending_edits;
    tess::OperationBatch ops;
    MixedBuildFn build_fn;
    std::optional<
        tess::AutoExecTask<MixedWorld, tess::WritePolicy::UniquePerChunk,
                           colony::BuildAck, MixedBuildFn>>
        build_task;
    tess::Schedule schedule;
    tess::SimClock sim_clock;
    tess::DeltaCollector deltas;
    std::vector<tess::Coord3> previous;
    std::uint64_t total_steps = 0;

    struct TopologyTask {
      MixedWorld* world = nullptr;
      tess::LocalTopologyScratch* scratch = nullptr;
      tess::RegionGraph* graph = nullptr;
      tess::PathAgentTickState* tick_state = nullptr;
      std::vector<tess::ChunkKey> dirty_scratch;
      auto operator()(const tess::ScheduleTaskContext&)
          -> tess::ScheduleTaskResult {
        dirty_scratch.clear();
        world->collect_dirty_chunks(colony::kTerrainDirty, dirty_scratch);
        if (!dirty_scratch.empty()) {
          (void)tess::update_region_graph<MixedWorld, colony::Walker>(
              *world, *scratch, *graph, dirty_scratch);
          tess::mark_pathing_dirty(*tick_state);
        }
        return {};
      }
    };
    struct AgentTask {
      MixedWorld* world = nullptr;
      std::span<tess::PathAgentState> agents;
      tess::PathRequestRuntime* runtime = nullptr;
      tess::PathAgentTickState* tick_state = nullptr;
      const tess::RegionGraph* graph = nullptr;
      colony::ColonyMovementTier tier = colony::ColonyMovementTier::Baseline;
      tess::PibtPriorities* pibt_priorities = nullptr;
      tess::JointMoveScratch* pibt_scratch = nullptr;
      auto operator()(const tess::ScheduleTaskContext&)
          -> tess::ScheduleTaskResult {
        const tess::PathAgentTickOptions options{.movement_dirty_mask =
                                                     colony::kOccupancyDirty};
        if (tier == colony::ColonyMovementTier::Pibt) {
          const tess::RouteAttachmentRanking rank{
              std::span<const tess::PathAgentState>{agents.data(),
                                                    agents.size()},
              &tick_state->routes};
          (void)tess::tick_weighted_path_agents_with_pibt<
              MixedWorld, colony::Walker, colony::kMaxCost,
              colony::OccupancyTag, colony::ReservationTag>(
              *tick_state, *world, agents, *runtime, *pibt_priorities,
              *pibt_scratch, rank, options,
              tess::JointMoveOptions{tess::SwapPolicy::Forbid}, graph);
          return {};
        }
        (void)tess::tick_weighted_path_agents_with_movement<
            MixedWorld, colony::Walker, colony::kMaxCost, colony::OccupancyTag,
            colony::ReservationTag>(*tick_state, *world, agents, *runtime,
                                    options, graph);
        return {};
      }
    };
    tess::PibtPriorities pibt_priorities;
    tess::JointMoveScratch pibt_scratch;
    // Churn edits actually applied during the run, in application order:
    // realized terrain history is tier-dependent (occupied tiles are
    // skipped), so the artifact hashes it separately from the static
    // candidate pool.
    std::vector<colony::TileEdit> realized_edits;
    TopologyTask topology_task;
    AgentTask agent_task;

    // One simulation tick, exactly the Colony::run() loop body. `tick`
    // is the controller's one-based tick; the harness tests churn
    // against its zero-based pre-advance loop index, so the harness
    // index is tick - 1 (verified by the equivalence check, which also
    // compares the production agent-flow counters that a one-tick churn
    // shift perturbs).
    void tick_body(std::uint64_t tick, bool churn_enabled) {
      const std::uint64_t harness_index = tick - 1;
      tess::observe_path_agent_flow_tick(tick_state, agents, tick);
      pending_edits.clear();
      if (churn_enabled && kMixedChurnPeriod > 0 && harness_index > 0 &&
          harness_index % kMixedChurnPeriod == 0 &&
          churn_cursor < churn_pool.size()) {
        for (std::uint32_t taken = 0;
             taken < kMixedChurnChunks && churn_cursor < churn_pool.size();
             ++churn_cursor) {
          const auto& edit = churn_pool[churn_cursor];
          if (world->template field<colony::OccupancyTag>(edit.coord)) {
            continue;
          }
          pending_edits.push_back(edit);
          ++taken;
        }
        std::vector<tess::ChunkKey> keys;
        for (const auto& edit : pending_edits) {
          const auto key = tess::chunk_key<WorldShape>(
              tess::chunk_coord<WorldShape>(edit.coord));
          if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            keys.push_back(key);
          }
        }
        for (const auto& key : keys) {
          (void)ops.update_field(
              tess::DomainDesc::explicit_chunks(
                  std::span<const tess::ChunkKey>{&key, 1}),
              tess::FieldAccessDesc{0, colony::kTerrainDirty.value,
                                    colony::kTerrainDirty},
              tess::WritePolicy::UniquePerChunk);
        }
        realized_edits.insert(realized_edits.end(), pending_edits.begin(),
                              pending_edits.end());
      }
      (void)schedule.run_tick(sim_clock);
      for (std::size_t i = 0; i < agents.size(); ++i) {
        if (agents[i].position != previous[i]) {
          ++total_steps;
          previous[i] = agents[i].position;
        }
      }
      tess::collect_tile_deltas(deltas, *world, colony::kTerrainDirty);
      (void)deltas.publish();
    }
  };

  [[nodiscard]] static auto build_mixed_stack(
      std::size_t population,
      colony::ColonyMovementTier tier = colony::ColonyMovementTier::Baseline)
      -> std::unique_ptr<MixedStack> {
    auto stack = std::make_unique<MixedStack>();
    const auto map = MixedColony::logical_map(kSeed);
    stack->world = std::make_unique<MixedWorld>();
    for (std::int64_t y = 0; y < kExtent; ++y) {
      for (std::int64_t x = 0; x < kExtent; ++x) {
        const auto logical_index =
            static_cast<std::size_t>(y / kSuiteScale) * map.width +
            static_cast<std::size_t>(x / kSuiteScale);
        const bool passable = map.passability[logical_index] != 0;
        const auto coord = tess::Coord3{x, y, 0};
        stack->world->template field<colony::PassableTag>(coord) = passable;
        stack->world->template field<colony::CostTag>(coord) =
            passable ? colony::detail::tile_cost(kSeed ^ 0xC057U, x, y) : 0U;
        stack->world->template field<colony::OccupancyTag>(coord) = false;
        stack->world->template field<colony::ReservationTag>(coord) = false;
      }
    }

    // Agents: the harness's scan-order placement, verbatim (the stride
    // mirrors ColonyConfig::placement_stride, proven equivalent by
    // validate_mixed_stack).
    const std::int64_t stride = kMixedPlacementStride;
    for (std::int64_t y = 1; y < kExtent && stack->agents.size() < population;
         y += stride) {
      for (std::int64_t x = 1; x + kMixedGoalDistance < kExtent &&
                               stack->agents.size() < population;
           x += kMixedGoalDistance + 2) {
        const auto start = tess::Coord3{x, y, 0};
        const auto goal = tess::Coord3{x + kMixedGoalDistance, y, 0};
        if (!stack->world->template field<colony::PassableTag>(start) ||
            !stack->world->template field<colony::PassableTag>(goal) ||
            stack->world->template field<colony::OccupancyTag>(start)) {
          continue;
        }
        tess::PathAgentState agent;
        agent.position = start;
        stack->world->template field<colony::OccupancyTag>(start) = true;
        stack->agents.push_back(agent);
        stack->assigned_goals.push_back(goal);
        stack->homes.push_back(start);
      }
    }
    if (stack->agents.size() != population) {
      char message[128];
      std::snprintf(message, sizeof(message),
                    "mixed colony under-placed agents: %zu of %zu seated "
                    "(placement stride %lld)",
                    stack->agents.size(), population,
                    static_cast<long long>(stride));
      fail(message);
    }

    stack->runtime.reserve_requests(stack->agents.size());
    stack->runtime.reserve_search_nodes(
        static_cast<std::size_t>(kExtent * kExtent));
    stack->runtime.reserve_path_nodes(stack->agents.size() * 64);
    stack->runtime.reserve_unit_routes(stack->agents.size());
    stack->tick_state.flow_accounting = &stack->agent_flow;

    tess::build_region_graph<MixedWorld, colony::Walker>(
        *stack->world, stack->topo_scratch, stack->graph);
    for (std::size_t i = 0; i < stack->agents.size(); ++i) {
      tess::set_path_agent_goal(stack->tick_state, stack->agents[i],
                                stack->assigned_goals[i]);
    }

    stack->ops.reserve_operations(kMixedChurnChunks + 4);
    const auto interior = colony::detail::interior_logical_tiles(map);
    {
      grid::SplitMix64 rng(kSeed ^ 0xC17U);
      std::vector<char> reserved(static_cast<std::size_t>(kExtent * kExtent),
                                 0);
      const auto mark = [&](tess::Coord3 coord) {
        reserved[static_cast<std::size_t>(coord.y * kExtent + coord.x)] = 1;
      };
      for (const auto& agent : stack->agents) {
        mark(agent.position);
      }
      for (const auto& goal : stack->assigned_goals) {
        mark(goal);
      }
      // Enough candidates for the longest budgeted run.
      const std::size_t wanted =
          (4096 / kMixedChurnPeriod + 1) * kMixedChurnChunks;
      for (std::size_t attempt = 0;
           attempt < interior.size() && stack->churn_pool.size() < wanted;
           ++attempt) {
        const auto logical_cell = interior[rng.below(interior.size())];
        const auto lx = static_cast<std::int64_t>(logical_cell % map.width);
        const auto ly = static_cast<std::int64_t>(logical_cell / map.width);
        const auto coord = tess::Coord3{lx * kSuiteScale + kSuiteScale / 2,
                                        ly * kSuiteScale + kSuiteScale / 2, 0};
        const auto flat = static_cast<std::size_t>(coord.y * kExtent + coord.x);
        if (reserved[flat] != 0) {
          continue;
        }
        reserved[flat] = 1;
        stack->churn_pool.push_back(colony::TileEdit{coord});
      }
    }

    stack->build_fn.pending = &stack->pending_edits;
    stack->build_task.emplace(*stack->world, stack->ops, stack->build_fn);
    stack->build_task->reserve_operations(kMixedChurnChunks + 4);
    stack->topology_task =
        typename MixedStack::TopologyTask{stack->world.get(),
                                          &stack->topo_scratch,
                                          &stack->graph,
                                          &stack->tick_state,
                                          {}};
    stack->pibt_priorities.reserve(stack->agents.size());
    stack->pibt_scratch.reserve(stack->agents.size());
    stack->agent_task = typename MixedStack::AgentTask{
        stack->world.get(),      std::span<tess::PathAgentState>{stack->agents},
        &stack->runtime,         &stack->tick_state,
        &stack->graph,           tier,
        &stack->pibt_priorities, &stack->pibt_scratch};

    stack->schedule.reserve_tasks(3);
    (void)stack->schedule.add_task(
        {"build", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
        *stack->build_task);
    (void)stack->schedule.add_task(
        {"topology", tess::SimPhase::Pathing,
         tess::Cadence::on_dirty(colony::kTerrainDirty)},
        stack->topology_task);
    (void)stack->schedule.add_task(
        {"agents", tess::SimPhase::Movement, tess::Cadence::every_tick()},
        stack->agent_task);
    stack->schedule.seal();
    stack->deltas.reserve(MixedWorld::chunk_count, 4096, 64);
    stack->previous.reserve(stack->agents.size());
    for (const auto& agent : stack->agents) {
      stack->previous.push_back(agent.position);
    }
    return stack;
  }

  // Stage-3 acceptance: before any budgeted cell runs, the mirrored
  // stack must reproduce the harness's reference run exactly — once per
  // movement tier, since the tier dispatch is part of the mirror. The
  // harness golden pins that the tiers diverge on this fixture (2566
  // vs 2838 total steps), so passing both proofs is non-vacuous.
  static void validate_mixed_stack(colony::ColonyMovementTier tier) {
    auto stack = build_mixed_stack(kMixedAgents, tier);
    for (std::uint64_t tick = 1; tick <= 40; ++tick) {
      stack->tick_body(tick, true);
    }

    colony::ColonyConfig config;
    config.agents = kMixedAgents;
    config.ticks = 40;
    config.churn_period = kMixedChurnPeriod;
    config.churn_chunks = kMixedChurnChunks;
    config.placement_stride = kMixedPlacementStride;
    config.movement_tier = tier;
    MixedColony reference(config);
    const colony::ColonyRun reference_run = reference.run();

    check(reference_run.agents_unplaced == 0,
          "reference colony under-placed agents");
    check(tier != colony::ColonyMovementTier::Pibt ||
              reference_run.counters.pibt_passes > 0,
          "pibt reference run never dispatched the pibt advance");
    check(reference_run.final_positions.size() == stack->agents.size(),
          "mixed stack population diverges from the harness");
    for (std::size_t i = 0; i < stack->agents.size(); ++i) {
      check(stack->agents[i].position == reference_run.final_positions[i],
            "mixed stack final positions diverge from Colony::run()");
    }
    check(stack->total_steps == reference_run.total_steps,
          "mixed stack step count diverges from Colony::run()");
    // The production path-agent flow counters are sensitive to churn and
    // replan timing: a one-tick shift anywhere in the loop perturbs them
    // even when final positions happen to coincide.
    const auto& mine = stack->agent_flow.counters;
    const auto& reference_flow = reference_run.agent_flow;
    check(mine.offered == reference_flow.offered &&
              mine.admitted == reference_flow.admitted &&
              mine.completed == reference_flow.completed &&
              mine.failed == reference_flow.failed &&
              mine.superseded == reference_flow.superseded &&
              mine.consumed_work_units == reference_flow.consumed_work_units,
          "mixed stack agent-flow counters diverge from Colony::run()");
  }

  // Per-agent ping-pong navigation items (design section 7.2).
  struct MixedDemand {
    struct Item {
      std::uint64_t admitted_tick = 0;
      std::uint64_t completed_tick = 0;
      bool completed = false;
      bool failed = false;
    };
    std::vector<Item> items;
    std::vector<std::size_t> open_item;  // Per agent; npos when idle.
    std::vector<tess::Coord3> current_goal;
    std::vector<std::uint8_t> heading_away;
    std::vector<std::size_t> rearm_queue;  // Agents to re-arm this tick.
    std::size_t oldest_pointer = 0;
    tess::diagnostics::FlowAccounting accounting;
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    void admit(MixedStack& stack, std::size_t agent, tess::Coord3 goal,
               std::uint64_t tick) {
      tess::set_path_agent_goal(stack.tick_state, stack.agents[agent], goal);
      current_goal[agent] = goal;
      ++accounting.counters.offered;
      accounting.record_admitted();
      open_item[agent] = items.size();
      items.push_back({tick, 0, false});
    }

    void observe(std::uint64_t tick) {
      accounting.observe_tick(tick);
      while (
          oldest_pointer < items.size() &&
          (items[oldest_pointer].completed || items[oldest_pointer].failed)) {
        ++oldest_pointer;
      }
      accounting.counters.oldest_outstanding_age_ticks =
          oldest_pointer < items.size()
              ? tick - items[oldest_pointer].admitted_tick
              : 0;
    }

    void fail(std::size_t agent, std::uint64_t tick) {
      Item& item = items[open_item[agent]];
      item.failed = true;
      item.completed_tick = tick;
      open_item[agent] = npos;
      ++accounting.counters.failed;
      accounting.record_left_outstanding();
      accounting.counters.residence_ticks_accumulated +=
          tick - item.admitted_tick;
    }

    void complete(std::size_t agent, std::uint64_t tick) {
      Item& item = items[open_item[agent]];
      item.completed = true;
      item.completed_tick = tick;
      open_item[agent] = npos;
      ++accounting.counters.completed;
      accounting.record_left_outstanding();
      accounting.counters.residence_ticks_accumulated +=
          tick - item.admitted_tick;
      accounting.counters.offered_work_units += 1;
      accounting.counters.consumed_work_units += 1;
    }
  };

  struct MixedCellSummary {
    std::uint64_t useful = 0;
    std::uint64_t cohort_admitted = 0;
    std::uint64_t cohort_met = 0;
    std::uint64_t starved = 0;
    std::vector<std::uint64_t> lateness;
  };

  [[nodiscard]] static auto summarize_mixed(const MixedDemand& demand,
                                            std::uint64_t window_start,
                                            std::uint64_t window_end,
                                            std::uint64_t final_tick,
                                            std::uint64_t base_tps)
      -> MixedCellSummary {
    MixedCellSummary out;
    const std::uint64_t starvation =
        std::max<std::uint64_t>(4 * kMixedAllowanceTicks, base_tps);
    for (const typename MixedDemand::Item& item : demand.items) {
      const std::uint64_t deadline = item.admitted_tick + kMixedAllowanceTicks;
      if (item.completed && item.completed_tick >= window_start &&
          item.completed_tick <= window_end) {
        ++out.useful;
      }
      if (item.admitted_tick < window_start ||
          item.admitted_tick > window_end) {
        continue;
      }
      ++out.cohort_admitted;
      // Every completed cohort item contributes a lateness sample —
      // zero when on time — so the artifact family's sample base really
      // is completed_cohort_items. Recording only positive lateness
      // made the published percentiles describe just the handful of
      // late completions (bug found in the 2026-08-15 campaign review).
      if (item.completed && item.completed_tick <= deadline) {
        ++out.cohort_met;
        out.lateness.push_back(0);
      }
      if (item.completed && item.completed_tick > deadline) {
        out.lateness.push_back(item.completed_tick - deadline);
      }
      const std::uint64_t served_or_now =
          (item.completed || item.failed) ? item.completed_tick : final_tick;
      if (served_or_now - item.admitted_tick >= starvation) {
        ++out.starved;
      }
    }
    return out;
  }

  static void run(const RunOptions& base_options) {
    namespace budgeted = tess_test::budgeted;
    std::vector<colony::ColonyMovementTier> tiers;
    for (const std::string& tier_name : base_options.mixed_tiers) {
      tiers.push_back(tier_name == "pibt"
                          ? colony::ColonyMovementTier::Pibt
                          : colony::ColonyMovementTier::Baseline);
    }
    for (const colony::ColonyMovementTier tier : tiers) {
      validate_mixed_stack(tier);
    }

    // A 24-tile goal needs ~25 simulation ticks before the first
    // arrival; the smoke configuration widens the mixed window (and
    // runs 60 TPS so ticks land every frame) so the completion and
    // re-arm paths are genuinely exercised rather than vacuously green.
    RunOptions options = base_options;
    if (options.measured_frames < 90) {
      options.measured_frames = 90;
    }

    // The full canonical matrix (design section 3.1): all seven budgets;
    // TPS and population axes come from --mixed-tps and
    // --mixed-populations. Smoke restricts to two budgets at 60 TPS.
    static constexpr std::array<Nanos, 7> kAllMixedBudgets = {
        125'000, 250'000, 500'000, 1'000'000, 2'000'000, 4'000'000, 8'000'000};
    static constexpr std::array<Nanos, 2> kSmokeMixedBudgets = {125'000,
                                                                8'000'000};
    const std::span<const Nanos> budgets =
        options.smoke ? std::span<const Nanos>{kSmokeMixedBudgets}
                      : std::span<const Nanos>{kAllMixedBudgets};
    const std::vector<std::uint32_t> tps_axis =
        options.smoke && !options.mixed_tps_explicit
            ? std::vector<std::uint32_t>{60}
            : options.mixed_tps;
    const std::vector<std::size_t> population_axis =
        options.smoke && !options.mixed_populations_explicit
            ? std::vector<std::size_t>{100}
            : options.mixed_populations;

    // Fail fast: seat the largest requested population before any cell
    // runs, so an unplaceable rung aborts the campaign up front rather
    // than deep into the matrix (the stage-4 timing pass died 40
    // minutes in on exactly that).
    if (!population_axis.empty()) {
      (void)build_mixed_stack(
          *std::max_element(population_axis.begin(), population_axis.end()));
    }

    struct MixedCellAccumulator {
      tess::diagnostics::FlowCounters window_flow{};
      std::vector<SaturatedRep> reps;
      MixedCellSummary totals;
      std::vector<std::uint64_t> lateness_pool;
      std::vector<std::uint64_t> oldest_samples;
      Nanos measured_wall = 0;
      std::uint64_t stable_reps = 0;
      std::uint64_t peak_rss = 0;
      // Hash of the churn edits actually applied, per repetition; the
      // dynamics are deterministic per cell, so every repetition must
      // realize the identical edit sequence.
      std::string realized_churn_sha256;
    };

    std::vector<const char*> views;
    if (options.mixed_view_fidelity) {
      views.push_back("mixed_current_fidelity");
    }
    if (options.mixed_view_quanta) {
      views.push_back("mixed_existing_quanta");
    }
    for (std::size_t tier_index = 0; tier_index < tiers.size(); ++tier_index) {
      const colony::ColonyMovementTier tier = tiers[tier_index];
      const char* tier_name =
          tier == colony::ColonyMovementTier::Pibt ? "pibt" : "baseline";
      for (const char* view : views) {
        for (const std::uint32_t tps : tps_axis) {
          for (const std::size_t population : population_axis) {
            SteadyClock clock;
            std::vector<MixedCellAccumulator> cells(budgets.size());
            // Repetition-outer with the budget start offset rotated by
            // repetition (design section 11.4): paced mixed cells occupy
            // long real-time spans, so budget order must decorrelate from
            // elapsed-time and thermal drift.
            for (std::uint64_t rep = 0; rep < options.repetitions; ++rep) {
              for (std::size_t k = 0; k < budgets.size(); ++k) {
                const std::size_t budget_index = (k + rep) % budgets.size();
                const Nanos budget = budgets[budget_index];
                MixedCellAccumulator& cell = cells[budget_index];

                auto stack = build_mixed_stack(population, tier);
                MixedDemand demand;
                demand.open_item.assign(population, MixedDemand::npos);
                demand.current_goal = stack->assigned_goals;
                demand.heading_away.assign(population, 1);
                // Upper-bound the ping-pong history so it can never
                // grow inside the timed frame loop: each agent's open
                // item plus one item per completed leg, where a leg
                // needs at least goal-distance+1 ticks of the run's
                // maximum tick budget.
                const std::uint64_t max_run_ticks =
                    (options.warmup_frames + options.measured_frames) * 8 +
                    kMixedSettlementTicks;
                const std::uint64_t max_legs_per_agent =
                    max_run_ticks / (kMixedGoalDistance + 1) + 2;
                demand.items.reserve(population * (max_legs_per_agent + 1));
                demand.rearm_queue.reserve(population);
                for (std::size_t agent = 0; agent < population; ++agent) {
                  ++demand.accounting.counters.offered;
                  demand.accounting.record_admitted();
                  demand.open_item[agent] = demand.items.size();
                  demand.items.push_back({0, 0, false, false});
                }

                FrameBudgetConfig config;
                config.budget_ns = budget;
                config.base_tps = tps;
                config.pacing = budgeted::Pacing::Paced;
                FrameBudgetController controller{clock, config};
                bool admitting = true;
                auto mandatory = [&](std::uint64_t tick) {
                  demand.observe(tick);
                  if (admitting) {
                    for (const std::size_t agent : demand.rearm_queue) {
                      const bool away = demand.heading_away[agent] != 0;
                      const tess::Coord3 next_goal =
                          away ? stack->homes[agent]
                               : stack->assigned_goals[agent];
                      demand.heading_away[agent] = away ? 0 : 1;
                      demand.admit(*stack, agent, next_goal, tick);
                    }
                  }
                  demand.rearm_queue.clear();
                  stack->tick_body(tick, true);
                  for (std::size_t agent = 0; agent < population; ++agent) {
                    if (demand.open_item[agent] == MixedDemand::npos) {
                      continue;
                    }
                    if (!stack->agents[agent].has_goal &&
                        stack->agents[agent].position ==
                            demand.current_goal[agent]) {
                      demand.complete(agent, tick);
                      demand.rearm_queue.push_back(agent);
                    } else if (stack->agents[agent].phase ==
                               tess::PathAgentPhase::Unreachable) {
                      demand.fail(agent, tick);
                      demand.heading_away[agent] =
                          demand.heading_away[agent] != 0 ? 0 : 1;
                      demand.rearm_queue.push_back(agent);
                    }
                  }
                };
                auto quantum = [&]() -> bool { return false; };

                for (std::uint64_t frame = 0; frame < options.warmup_frames;
                     ++frame) {
                  (void)controller.run_frame(mandatory, quantum);
                }
                const tess::diagnostics::FlowCounters window_start_flow =
                    demand.accounting.counters;
                const std::uint64_t window_start_tick =
                    controller.sim_tick() + 1;
                SaturatedRep samples;
                samples.frame_elapsed_ns.reserve(options.measured_frames);
                samples.overshoot_quantum_tail_ns.reserve(
                    options.measured_frames);
                samples.overshoot_mandatory_ns.reserve(options.measured_frames);
                samples.frame_start_lag_ns.reserve(options.measured_frames);
                const Nanos wall_start = clock.now();
                for (std::uint64_t frame = 0; frame < options.measured_frames;
                     ++frame) {
                  const FrameRecord record =
                      controller.run_frame(mandatory, quantum);
                  samples.frame_elapsed_ns.push_back(record.elapsed_ns);
                  samples.overshoot_quantum_tail_ns.push_back(
                      record.overshoot_quantum_tail_ns);
                  samples.overshoot_mandatory_ns.push_back(
                      record.overshoot_mandatory_ns);
                  samples.frame_start_lag_ns.push_back(
                      record.frame_start_lag_ns);
                  if (record.overshoot_quantum_tail_ns > 0 ||
                      record.overshoot_mandatory_ns > 0) {
                    ++samples.overshoot_frames;
                  }
                  cell.oldest_samples.push_back(
                      demand.accounting.counters.oldest_outstanding_age_ticks);
                }
                cell.measured_wall +=
                    budgeted::sub_clamped(clock.now(), wall_start);
                const std::uint64_t window_end_tick = controller.sim_tick();
                demand.observe(window_end_tick);
                const tess::diagnostics::FlowCounters window_end_flow =
                    demand.accounting.counters;

                // Closed-loop window flow with carried inventory: items
                // open at window start count as window admissions, so the
                // artifact's conservation identities hold exactly without
                // unsigned underflow across the live boundary (the window
                // never starts quiescent in a ping-pong colony).
                {
                  tess::diagnostics::FlowCounters delta{};
                  delta.offered = window_end_flow.offered -
                                  window_start_flow.offered +
                                  window_start_flow.outstanding_current;
                  delta.admitted = window_end_flow.admitted -
                                   window_start_flow.admitted +
                                   window_start_flow.outstanding_current;
                  delta.completed =
                      window_end_flow.completed - window_start_flow.completed;
                  delta.failed =
                      window_end_flow.failed - window_start_flow.failed;
                  delta.consumed_work_units =
                      window_end_flow.consumed_work_units -
                      window_start_flow.consumed_work_units;
                  delta.offered_work_units =
                      window_end_flow.offered_work_units -
                      window_start_flow.offered_work_units;
                  delta.residence_ticks_accumulated =
                      window_end_flow.residence_ticks_accumulated -
                      window_start_flow.residence_ticks_accumulated;
                  delta.inventory_tick_weighted =
                      window_end_flow.inventory_tick_weighted -
                      window_start_flow.inventory_tick_weighted;
                  delta.outstanding_current =
                      window_end_flow.outstanding_current;
                  delta.outstanding_high_water =
                      window_end_flow.outstanding_current >
                              window_start_flow.outstanding_current
                          ? window_end_flow.outstanding_current
                          : window_start_flow.outstanding_current;
                  delta.oldest_outstanding_age_ticks =
                      window_end_flow.oldest_outstanding_age_ticks;
                  accumulate_window(cell.window_flow,
                                    tess::diagnostics::FlowCounters{}, delta);
                }

                admitting = false;
                const std::uint64_t settle_until =
                    window_end_tick + kMixedSettlementTicks;
                while (controller.sim_tick() < settle_until) {
                  (void)controller.run_frame(mandatory, quantum);
                }
                const MixedCellSummary rep_summary =
                    summarize_mixed(demand, window_start_tick, window_end_tick,
                                    controller.sim_tick(), tps);
                samples.useful_completions = rep_summary.useful;

                const bool identities =
                    window_end_flow.admission_identity_holds() &&
                    window_end_flow.retention_identity_holds();
                const bool age_ok =
                    window_end_flow.oldest_outstanding_age_ticks <=
                    std::max<std::uint64_t>(4 * kMixedAllowanceTicks, tps);
                const bool deadline_ok = rep_summary.cohort_admitted == 0 ||
                                         rep_summary.cohort_met * 100 >=
                                             rep_summary.cohort_admitted * 99;
                if (identities && age_ok && deadline_ok) {
                  ++cell.stable_reps;
                }
                cell.totals.useful += rep_summary.useful;
                cell.totals.cohort_admitted += rep_summary.cohort_admitted;
                cell.totals.cohort_met += rep_summary.cohort_met;
                cell.totals.starved += rep_summary.starved;
                cell.lateness_pool.insert(cell.lateness_pool.end(),
                                          rep_summary.lateness.begin(),
                                          rep_summary.lateness.end());
                cell.reps.push_back(std::move(samples));
                cell.peak_rss = std::max(cell.peak_rss, current_rss_bytes());
                {
                  budgeted::Sha256 realized_hasher;
                  const char* realized_tag = "realized_churn_v1";
                  realized_hasher.update(realized_tag,
                                         std::strlen(realized_tag));
                  for (const colony::TileEdit& edit : stack->realized_edits) {
                    const std::int64_t words[2] = {edit.coord.x, edit.coord.y};
                    realized_hasher.update(words, sizeof(words));
                  }
                  const std::string realized = realized_hasher.hex_digest();
                  if (cell.realized_churn_sha256.empty()) {
                    cell.realized_churn_sha256 = realized;
                  } else {
                    check(cell.realized_churn_sha256 == realized,
                          "realized churn edits diverged between repetitions "
                          "of one mixed cell");
                  }
                }
              }
            }

            for (std::size_t i = 0; i < budgets.size(); ++i) {
              MixedCellAccumulator& cell = cells[i];
              budgeted::Artifact artifact;
              {
                SaturatedCellResult shim;
                shim.window_flow = cell.window_flow;
                shim.reps = cell.reps;
                shim.peak_rss = cell.peak_rss;
                artifact = build_artifact(options, shim, budgets[i]);
              }
              artifact.experiment.kind = view;
              // The tier is part of the scenario identity so every consumer
              // that keys on scenario_id separates the cohorts, including
              // ones that predate the movement_tier field.
              // World extent and tier are both part of the scenario
              // identity; the 512 world keeps its historical ids.
              artifact.experiment.scenario_id =
                  std::string{"colony-roomcorridor-pingpong"} +
                  (kExtent == 512 ? "" : "-" + std::to_string(kExtent)) +
                  (tier == colony::ColonyMovementTier::Pibt ? "-pibt" : "") +
                  "-v1";
              artifact.experiment.movement_tier = tier_name;
              artifact.experiment.workload_refs = {
                  "path/agent", "topology/region_graph", "queued/execute"};
              artifact.experiment.sim_tps = tps;
              artifact.experiment.population = population;
              artifact.experiment.pacing = "paced";
              artifact.experiment.settlement_ticks = kMixedSettlementTicks;
              artifact.summary.useful_completions = cell.totals.useful;

              std::vector<std::uint64_t> lag_pool;
              for (const SaturatedRep& rep : cell.reps) {
                lag_pool.insert(lag_pool.end(), rep.frame_start_lag_ns.begin(),
                                rep.frame_start_lag_ns.end());
              }
              artifact.summary.frame_start_lag_ns = budgeted::summarize_family(
                  "paced_measured_frames_pooled", std::move(lag_pool));
              artifact.summary.measured_wall_ns = cell.measured_wall;
              const double wall_seconds =
                  static_cast<double>(cell.measured_wall) / 1e9;
              artifact.summary.useful_per_wall_second =
                  wall_seconds > 0
                      ? static_cast<double>(cell.totals.useful) / wall_seconds
                      : 0.0;

              budgeted::DeadlineGroup deadlines;
              deadlines.deadline_success_rate =
                  cell.totals.cohort_admitted > 0
                      ? static_cast<double>(cell.totals.cohort_met) /
                            static_cast<double>(cell.totals.cohort_admitted)
                      : 1.0;
              deadlines.lateness_ticks = budgeted::summarize_family(
                  "completed_cohort_items", cell.lateness_pool);
              deadlines.oldest_age_ticks = budgeted::summarize_family(
                  "per_tick_window_observations", cell.oldest_samples);
              deadlines.starved_items = cell.totals.starved;
              artifact.summary.deadlines = deadlines;
              artifact.summary.flow_stable_applicable = true;
              artifact.summary.flow_stable =
                  cell.stable_reps * 2 > options.repetitions;

              budgeted::ClassArtifact interactive;
              interactive.class_id = "interactive_path";
              interactive.deadline_allowance_ticks = kMixedAllowanceTicks;
              interactive.useful_completions = cell.totals.useful;
              interactive.cohort_admitted = cell.totals.cohort_admitted;
              interactive.deadline_success_rate =
                  deadlines.deadline_success_rate;
              interactive.lateness_ticks = deadlines.lateness_ticks;
              interactive.starved_items = cell.totals.starved;
              artifact.classes.push_back(interactive);

              // The trace identity covers the materialized workload: the
              // logical map text, every agent's endpoints, and the full
              // churn candidate pool.
              {
                auto reference = build_mixed_stack(population);
                budgeted::Sha256 hasher;
                const char* tag = "colony_pingpong_v2";
                hasher.update(tag, std::strlen(tag));
                const std::string map_text =
                    gen::room_and_corridor(kLogicalExtent, kLogicalExtent,
                                           kSeed, {24, 6, 12})
                        ->text;
                hasher.update(map_text.data(), map_text.size());
                for (std::size_t agent = 0; agent < population; ++agent) {
                  const std::int64_t words[4] = {
                      reference->homes[agent].x, reference->homes[agent].y,
                      reference->assigned_goals[agent].x,
                      reference->assigned_goals[agent].y};
                  hasher.update(words, sizeof(words));
                }
                for (const colony::TileEdit& edit : reference->churn_pool) {
                  const std::int64_t words[2] = {edit.coord.x, edit.coord.y};
                  hasher.update(words, sizeof(words));
                }
                // The five-word parameter encoding is versioned by
                // the colony_pingpong_v2 tag and must stay byte-stable
                // for the historical 512 world; the extent word
                // extends the encoding only for the newer worlds.
                const std::uint64_t params[5] = {
                    kSeed, population, kMixedChurnPeriod, kMixedAllowanceTicks,
                    static_cast<std::uint64_t>(tier)};
                hasher.update(params, sizeof(params));
                if (kExtent != 512) {
                  const std::uint64_t extent_word =
                      static_cast<std::uint64_t>(kExtent);
                  hasher.update(&extent_word, sizeof(extent_word));
                }
                artifact.trace.sha256 = hasher.hex_digest();
                artifact.trace.realized_churn_sha256 =
                    cell.realized_churn_sha256;
              }
              SteadyClock calibration_clock;
              artifact.calibration = calibrate(calibration_clock);
              const std::string name =
                  std::string{"mixed_"} +
                  (std::string{view} == "mixed_current_fidelity" ? "fidelity"
                                                                 : "quanta") +
                  "_" + tier_name +
                  (kExtent == 512 ? "" : "_w" + std::to_string(kExtent)) +
                  "_p" + std::to_string(population) + "_" +
                  std::to_string(tps) + "tps";
              write_artifact(options, artifact, name, budgets[i]);
            }
          }
        }
      }
    }
  }
};

using Shape1024 =
    tess::Shape<tess::Extent3{1024, 1024, 1}, tess::Extent3{32, 32, 1}>;

}  // namespace

void run_mixed_colony_cells(const RunOptions& base_options) {
  if (base_options.mixed_world == 1024) {
    MixedSuite<Shape1024>::run(base_options);
    return;
  }
  MixedSuite<PathScaleShape>::run(base_options);
}

}  // namespace bpb_bench

// Stage 3 of the budgeted-progress suite: the 60 Hz mixed colony
// (design section 7). Split into its own translation unit to keep
// each source under the repository's per-file token budget; shared
// plumbing lives in budgeted_progress_bench_common.h.

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
using MixedWorld = tess::AlwaysResidentWorld<PathScaleShape, MixedSchema>;
using MixedColony = colony::Colony<PathScaleShape, MixedSchema>;

constexpr std::size_t kMixedAgents = 100;
constexpr std::uint32_t kMixedChurnPeriod = 8;
constexpr std::uint32_t kMixedChurnChunks = 4;
constexpr std::int64_t kMixedGoalDistance = 24;
constexpr std::uint64_t kMixedAllowanceTicks = 32;
constexpr std::uint64_t kMixedSettlementTicks = 2 * kMixedAllowanceTicks;

struct MixedBuildFn {
  std::vector<colony::TileEdit>* pending = nullptr;
  auto operator()(auto view, colony::BuildAck& ack) -> void {
    auto passable = view.template field_span<colony::PassableTag>();
    auto cost = view.template field_span<colony::CostTag>();
    for (const auto& edit : *pending) {
      if (tess::chunk_key<PathScaleShape>(
              tess::chunk_coord<PathScaleShape>(edit.coord)) != view.key()) {
        continue;
      }
      const auto local = tess::local_tile_id<PathScaleShape>(
          tess::local_coord<PathScaleShape>(edit.coord));
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
  tess::FrameOps ops;
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
    auto operator()(const tess::ScheduleTaskContext&)
        -> tess::ScheduleTaskResult {
      (void)tess::tick_weighted_path_agents_with_movement<
          MixedWorld, colony::Walker, colony::kMaxCost, colony::OccupancyTag,
          colony::ReservationTag>(*tick_state, *world, agents, *runtime, {},
                                  colony::kOccupancyDirty, graph);
      return {};
    }
  };
  TopologyTask topology_task;
  AgentTask agent_task;

  // One simulation tick, exactly the Colony::run() loop body.
  void tick_body(std::uint64_t tick, bool churn_enabled) {
    tess::observe_path_agent_flow_tick(tick_state, agents, tick);
    pending_edits.clear();
    if (churn_enabled && kMixedChurnPeriod > 0 && tick > 0 &&
        tick % kMixedChurnPeriod == 0 && churn_cursor < churn_pool.size()) {
      for (std::uint32_t taken = 0;
           taken < kMixedChurnChunks && churn_cursor < churn_pool.size();
           ++churn_cursor) {
        const auto& edit = churn_pool[churn_cursor];
        if (world->field<colony::OccupancyTag>(edit.coord)) {
          continue;
        }
        pending_edits.push_back(edit);
        ++taken;
      }
      std::vector<tess::ChunkKey> keys;
      for (const auto& edit : pending_edits) {
        const auto key = tess::chunk_key<PathScaleShape>(
            tess::chunk_coord<PathScaleShape>(edit.coord));
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
          keys.push_back(key);
        }
      }
      for (const auto& key : keys) {
        (void)ops.update_field(tess::DomainDesc::explicit_chunks(
                                   std::span<const tess::ChunkKey>{&key, 1}),
                               tess::FieldAccessDesc{0, colony::kTerrainDirty,
                                                     colony::kTerrainDirty},
                               tess::WritePolicy::UniquePerChunk);
      }
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

[[nodiscard]] auto build_mixed_stack() -> std::unique_ptr<MixedStack> {
  auto stack = std::make_unique<MixedStack>();
  const auto map = MixedColony::logical_map(kSeed);
  stack->world = std::make_unique<MixedWorld>();
  for (std::int64_t y = 0; y < 512; ++y) {
    for (std::int64_t x = 0; x < 512; ++x) {
      const auto logical_index =
          static_cast<std::size_t>(y / kScale) * map.width +
          static_cast<std::size_t>(x / kScale);
      const bool passable = map.passability[logical_index] != 0;
      const auto coord = tess::Coord3{x, y, 0};
      stack->world->field<colony::PassableTag>(coord) = passable;
      stack->world->field<colony::CostTag>(coord) =
          passable ? colony::detail::tile_cost(kSeed ^ 0xC057U, x, y) : 0U;
      stack->world->field<colony::OccupancyTag>(coord) = false;
      stack->world->field<colony::ReservationTag>(coord) = false;
    }
  }

  // Agents: the harness's scan-order placement, verbatim.
  const std::int64_t stride = std::max<std::int64_t>(1, 512 / 64);
  for (std::int64_t y = 1; y < 512 && stack->agents.size() < kMixedAgents;
       y += stride) {
    for (std::int64_t x = 1;
         x + kMixedGoalDistance < 512 && stack->agents.size() < kMixedAgents;
         x += kMixedGoalDistance + 2) {
      const auto start = tess::Coord3{x, y, 0};
      const auto goal = tess::Coord3{x + kMixedGoalDistance, y, 0};
      if (!stack->world->field<colony::PassableTag>(start) ||
          !stack->world->field<colony::PassableTag>(goal) ||
          stack->world->field<colony::OccupancyTag>(start)) {
        continue;
      }
      tess::PathAgentState agent;
      agent.position = start;
      stack->world->field<colony::OccupancyTag>(start) = true;
      stack->agents.push_back(agent);
      stack->assigned_goals.push_back(goal);
      stack->homes.push_back(start);
    }
  }
  check(stack->agents.size() == kMixedAgents,
        "mixed colony under-placed agents");

  stack->runtime.reserve_requests(stack->agents.size());
  stack->runtime.reserve_search_nodes(512 * 512);
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
  const auto interior = colony::detail::interior_logical_cells(map);
  {
    grid::SplitMix64 rng(kSeed ^ 0xC17U);
    std::vector<char> reserved(512 * 512, 0);
    const auto mark = [&](tess::Coord3 coord) {
      reserved[static_cast<std::size_t>(coord.y * 512 + coord.x)] = 1;
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
      const auto coord =
          tess::Coord3{lx * kScale + kScale / 2, ly * kScale + kScale / 2, 0};
      const auto flat = static_cast<std::size_t>(coord.y * 512 + coord.x);
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
  stack->topology_task = MixedStack::TopologyTask{stack->world.get(),
                                                  &stack->topo_scratch,
                                                  &stack->graph,
                                                  &stack->tick_state,
                                                  {}};
  stack->agent_task = MixedStack::AgentTask{
      stack->world.get(), std::span<tess::PathAgentState>{stack->agents},
      &stack->runtime, &stack->tick_state, &stack->graph};

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
// stack must reproduce the harness's reference run exactly.
void validate_mixed_stack() {
  auto stack = build_mixed_stack();
  for (std::uint64_t tick = 1; tick <= 40; ++tick) {
    stack->tick_body(tick, true);
  }

  colony::ColonyConfig config;
  config.agents = kMixedAgents;
  config.ticks = 40;
  config.churn_period = kMixedChurnPeriod;
  config.churn_chunks = kMixedChurnChunks;
  MixedColony reference(config);
  const colony::ColonyRun reference_run = reference.run();

  check(reference_run.agents_unplaced == 0,
        "reference colony under-placed agents");
  check(reference_run.final_positions.size() == stack->agents.size(),
        "mixed stack population diverges from the harness");
  for (std::size_t i = 0; i < stack->agents.size(); ++i) {
    check(stack->agents[i].position == reference_run.final_positions[i],
          "mixed stack final positions diverge from Colony::run()");
  }
  check(stack->total_steps == reference_run.total_steps,
        "mixed stack step count diverges from Colony::run()");
}

// Per-agent ping-pong navigation items (design section 7.2).
struct MixedDemand {
  struct Item {
    std::uint64_t admitted_tick = 0;
    std::uint64_t completed_tick = 0;
    bool completed = false;
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
    while (oldest_pointer < items.size() && items[oldest_pointer].completed) {
      ++oldest_pointer;
    }
    accounting.counters.oldest_outstanding_age_ticks =
        oldest_pointer < items.size()
            ? tick - items[oldest_pointer].admitted_tick
            : 0;
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

[[nodiscard]] auto summarize_mixed(const MixedDemand& demand,
                                   std::uint64_t window_start,
                                   std::uint64_t window_end,
                                   std::uint64_t final_tick,
                                   std::uint64_t base_tps) -> MixedCellSummary {
  MixedCellSummary out;
  const std::uint64_t starvation =
      std::max<std::uint64_t>(4 * kMixedAllowanceTicks, base_tps);
  for (const MixedDemand::Item& item : demand.items) {
    const std::uint64_t deadline = item.admitted_tick + kMixedAllowanceTicks;
    if (item.completed && item.completed_tick >= window_start &&
        item.completed_tick <= window_end) {
      ++out.useful;
    }
    if (item.admitted_tick < window_start || item.admitted_tick > window_end) {
      continue;
    }
    ++out.cohort_admitted;
    if (item.completed && item.completed_tick <= deadline) {
      ++out.cohort_met;
    }
    if (item.completed && item.completed_tick > deadline) {
      out.lateness.push_back(item.completed_tick - deadline);
    }
    const std::uint64_t served_or_now =
        item.completed ? item.completed_tick : final_tick;
    if (served_or_now - item.admitted_tick >= starvation) {
      ++out.starved;
    }
  }
  return out;
}

}  // namespace

void run_mixed_colony_cells(const RunOptions& base_options) {
  namespace budgeted = tess_test::budgeted;
  validate_mixed_stack();

  // A 24-tile goal needs ~25 simulation ticks before the first
  // arrival; at 20 TPS that is ~75 frames. The smoke configuration
  // widens the mixed window so the completion and re-arm paths are
  // genuinely exercised rather than vacuously green.
  RunOptions options = base_options;
  if (options.measured_frames < 90) {
    options.measured_frames = 90;
  }

  const std::array<const char*, 2> kViews = {"mixed_current_fidelity",
                                             "mixed_existing_quanta"};
  constexpr std::uint32_t kMixedTps = 20;
  for (const char* view : kViews) {
    SteadyClock clock;
    for (std::size_t budget_index = 0; budget_index < kBudgetsNs.size();
         ++budget_index) {
      const Nanos budget = kBudgetsNs[budget_index];
      tess::diagnostics::FlowCounters window_flow{};
      std::vector<SaturatedRep> reps;
      MixedCellSummary totals;
      std::vector<std::uint64_t> lateness_pool;
      std::vector<std::uint64_t> oldest_samples;
      Nanos measured_wall = 0;
      std::uint64_t stable_reps = 0;
      std::uint64_t peak_rss = 0;

      for (std::uint64_t rep = 0; rep < options.repetitions; ++rep) {
        auto stack = build_mixed_stack();
        MixedDemand demand;
        demand.open_item.assign(kMixedAgents, MixedDemand::npos);
        demand.current_goal = stack->assigned_goals;
        demand.heading_away.assign(kMixedAgents, 1);
        demand.items.reserve(4096);
        demand.rearm_queue.reserve(kMixedAgents);
        // The setup already armed the initial away goals: account them
        // as tick-zero admissions.
        for (std::size_t agent = 0; agent < kMixedAgents; ++agent) {
          ++demand.accounting.counters.offered;
          demand.accounting.record_admitted();
          demand.open_item[agent] = demand.items.size();
          demand.items.push_back({0, 0, false});
        }

        FrameBudgetConfig config;
        config.budget_ns = budget;
        config.base_tps = kMixedTps;
        config.pacing = budgeted::Pacing::Paced;
        FrameBudgetController controller{clock, config};
        bool admitting = true;
        auto mandatory = [&](std::uint64_t tick) {
          demand.observe(tick);
          // Re-arms scheduled on the previous tick's arrivals: the
          // opposite endpoint, armed on the tick after arrival.
          if (admitting) {
            for (const std::size_t agent : demand.rearm_queue) {
              const bool away = demand.heading_away[agent] != 0;
              const tess::Coord3 next_goal =
                  away ? stack->homes[agent] : stack->assigned_goals[agent];
              demand.heading_away[agent] = away ? 0 : 1;
              demand.admit(*stack, agent, next_goal, tick);
            }
          }
          demand.rearm_queue.clear();
          stack->tick_body(tick, true);
          for (std::size_t agent = 0; agent < kMixedAgents; ++agent) {
            if (demand.open_item[agent] != MixedDemand::npos &&
                !stack->agents[agent].has_goal &&
                stack->agents[agent].position == demand.current_goal[agent]) {
              demand.complete(agent, tick);
              demand.rearm_queue.push_back(agent);
            }
          }
        };
        // Every task in this stack is tick-coupled: the defer-capable
        // set is empty in both views (see the header comment).
        auto quantum = [&]() -> bool { return false; };

        std::uint64_t frames_before_window = 0;
        for (std::uint64_t frame = 0; frame < options.warmup_frames; ++frame) {
          (void)controller.run_frame(mandatory, quantum);
          ++frames_before_window;
        }
        const tess::diagnostics::FlowCounters window_start_flow =
            demand.accounting.counters;
        const std::uint64_t window_start_tick = controller.sim_tick() + 1;
        SaturatedRep samples;
        samples.frame_elapsed_ns.reserve(options.measured_frames);
        samples.overshoot_quantum_tail_ns.reserve(options.measured_frames);
        samples.overshoot_mandatory_ns.reserve(options.measured_frames);
        samples.frame_start_lag_ns.reserve(options.measured_frames);
        const Nanos wall_start = clock.now();
        for (std::uint64_t frame = 0; frame < options.measured_frames;
             ++frame) {
          const FrameRecord record = controller.run_frame(mandatory, quantum);
          samples.frame_elapsed_ns.push_back(record.elapsed_ns);
          samples.overshoot_quantum_tail_ns.push_back(
              record.overshoot_quantum_tail_ns);
          samples.overshoot_mandatory_ns.push_back(
              record.overshoot_mandatory_ns);
          samples.frame_start_lag_ns.push_back(record.frame_start_lag_ns);
          if (record.overshoot_quantum_tail_ns > 0 ||
              record.overshoot_mandatory_ns > 0) {
            ++samples.overshoot_frames;
          }
          oldest_samples.push_back(
              demand.accounting.counters.oldest_outstanding_age_ticks);
        }
        measured_wall += budgeted::sub_clamped(clock.now(), wall_start);
        const std::uint64_t window_end_tick = controller.sim_tick();
        demand.observe(window_end_tick);
        const tess::diagnostics::FlowCounters window_end_flow =
            demand.accounting.counters;
        accumulate_window(window_flow, window_start_flow, window_end_flow);

        // Settlement: admissions stop, ticks keep running.
        admitting = false;
        const std::uint64_t settle_until =
            window_end_tick + kMixedSettlementTicks;
        while (controller.sim_tick() < settle_until) {
          (void)controller.run_frame(mandatory, quantum);
        }
        const MixedCellSummary rep_summary =
            summarize_mixed(demand, window_start_tick, window_end_tick,
                            controller.sim_tick(), kMixedTps);
        samples.useful_completions = rep_summary.useful;

        // Closed-loop verdict (design sections 7.2/9.2): identities
        // and growth are validity checks; deadline and age carry it.
        const bool identities = window_end_flow.admission_identity_holds() &&
                                window_end_flow.retention_identity_holds();
        const bool age_ok =
            window_end_flow.oldest_outstanding_age_ticks <=
            std::max<std::uint64_t>(4 * kMixedAllowanceTicks, kMixedTps);
        const bool deadline_ok =
            rep_summary.cohort_admitted == 0 ||
            rep_summary.cohort_met * 100 >= rep_summary.cohort_admitted * 99;
        if (identities && age_ok && deadline_ok) {
          ++stable_reps;
        }
        totals.useful += rep_summary.useful;
        totals.cohort_admitted += rep_summary.cohort_admitted;
        totals.cohort_met += rep_summary.cohort_met;
        totals.starved += rep_summary.starved;
        lateness_pool.insert(lateness_pool.end(), rep_summary.lateness.begin(),
                             rep_summary.lateness.end());
        reps.push_back(std::move(samples));
        peak_rss = std::max(peak_rss, current_rss_bytes());
        (void)frames_before_window;
      }

      budgeted::Artifact artifact;
      {
        SaturatedCellResult shim;
        shim.window_flow = window_flow;
        shim.reps = reps;
        shim.peak_rss = peak_rss;
        artifact = build_artifact(options, shim, budget);
      }
      artifact.experiment.kind = view;
      artifact.experiment.scenario_id = "colony-roomcorridor-pingpong-v1";
      artifact.experiment.workload_refs = {
          "path/agent", "topology/region_graph", "queued/execute"};
      artifact.experiment.sim_tps = kMixedTps;
      artifact.experiment.pacing = "paced";
      artifact.experiment.settlement_ticks = kMixedSettlementTicks;
      artifact.summary.useful_completions = totals.useful;

      std::vector<std::uint64_t> lag_pool;
      for (const SaturatedRep& rep : reps) {
        lag_pool.insert(lag_pool.end(), rep.frame_start_lag_ns.begin(),
                        rep.frame_start_lag_ns.end());
      }
      artifact.summary.frame_start_lag_ns = budgeted::summarize_family(
          "paced_measured_frames_pooled", std::move(lag_pool));
      artifact.summary.measured_wall_ns = measured_wall;
      const double wall_seconds = static_cast<double>(measured_wall) / 1e9;
      artifact.summary.useful_per_wall_second =
          wall_seconds > 0 ? static_cast<double>(totals.useful) / wall_seconds
                           : 0.0;

      budgeted::DeadlineGroup deadlines;
      deadlines.deadline_success_rate =
          totals.cohort_admitted > 0
              ? static_cast<double>(totals.cohort_met) /
                    static_cast<double>(totals.cohort_admitted)
              : 1.0;
      deadlines.lateness_ticks =
          budgeted::summarize_family("completed_cohort_items", lateness_pool);
      deadlines.oldest_age_ticks = budgeted::summarize_family(
          "per_tick_window_observations", oldest_samples);
      deadlines.starved_items = totals.starved;
      artifact.summary.deadlines = deadlines;
      artifact.summary.flow_stable_applicable = true;
      artifact.summary.flow_stable = stable_reps * 2 > options.repetitions;

      budgeted::ClassArtifact interactive;
      interactive.class_id = "interactive_path";
      interactive.deadline_allowance_ticks = kMixedAllowanceTicks;
      interactive.useful_completions = totals.useful;
      interactive.cohort_admitted = totals.cohort_admitted;
      interactive.deadline_success_rate = deadlines.deadline_success_rate;
      interactive.lateness_ticks = deadlines.lateness_ticks;
      interactive.starved_items = totals.starved;
      artifact.classes.push_back(interactive);

      budgeted::Sha256 hasher;
      const char* tag = "colony_pingpong_v1";
      hasher.update(tag, std::strlen(tag));
      const std::uint64_t words[5] = {kSeed, kMixedAgents, kMixedChurnPeriod,
                                      kMixedChurnChunks, kMixedAllowanceTicks};
      hasher.update(words, sizeof(words));
      artifact.trace.sha256 = hasher.hex_digest();
      SteadyClock calibration_clock;
      artifact.calibration = calibrate(calibration_clock);
      const std::string name =
          std::string{"mixed_"} + (std::string{view} == "mixed_current_fidelity"
                                       ? "fidelity"
                                       : "quanta");
      write_artifact(options, artifact, name, budget);
    }
  }
}

}  // namespace bpb_bench

#include <charconv>
#include <string_view>

#include "colony_endpoint_guard_fixture.h"
#include "colony_model_internal.h"

namespace tess::examples::web_colony {

namespace {

// Native-only evidence runner for the tutorial model. It deliberately uses
// the same public construction, wall-queue, tick, and diagnostic operations
// available to an adopter; browser wiring and simulation policy stay in their
// focused tutorial files.
struct NativeScenarioOptions {
  std::string_view scenario;
  int agents = kMaxAgents;
  int max_ticks = 5000;
  bool spread = true;
  bool require_complete = false;
};

void print_native_scenario_usage() {
  std::cout << "usage: tess_web_colony_model --scenario "
               "<open|tip|two-gates|four-gates|goal-wall|browser-guard|"
               "browser-incremental>\n"
               "       [--agents 1..1024] [--mode canonical|spread]\n"
               "       [--max-ticks N] [--require-complete]\n";
}

[[nodiscard]] auto parse_positive_int(std::string_view text, int& value)
    -> bool {
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end && value > 0;
}

[[nodiscard]] auto supported_scenario(std::string_view scenario) -> bool {
  return scenario == "open" || scenario == "tip" || scenario == "two-gates" ||
         scenario == "four-gates" || scenario == "goal-wall" ||
         scenario == "browser-guard" || scenario == "browser-incremental";
}

[[nodiscard]] auto parse_native_scenario_options(int argc, char** argv,
                                                 NativeScenarioOptions& options)
    -> bool {
  for (auto i = 1; i < argc; ++i) {
    const auto argument = std::string_view{argv[i]};
    const auto next_value = [&]() -> std::string_view {
      if (i + 1 >= argc) {
        return {};
      }
      return argv[++i];
    };
    if (argument == "--scenario") {
      options.scenario = next_value();
    } else if (argument == "--agents") {
      if (!parse_positive_int(next_value(), options.agents)) {
        return false;
      }
    } else if (argument == "--max-ticks") {
      if (!parse_positive_int(next_value(), options.max_ticks)) {
        return false;
      }
    } else if (argument == "--mode") {
      const auto mode = next_value();
      if (mode == "canonical") {
        options.spread = false;
      } else if (mode == "spread") {
        options.spread = true;
      } else {
        return false;
      }
    } else if (argument == "--require-complete") {
      options.require_complete = true;
    } else {
      return false;
    }
  }
  return supported_scenario(options.scenario) && options.agents <= kMaxAgents;
}

[[nodiscard]] auto queue_native_scenario_walls(ColonyModel& model,
                                               std::string_view scenario)
    -> std::size_t {
  auto accepted = std::size_t{0};
  if (scenario == "browser-guard" || scenario == "browser-incremental") {
    for (const auto& [x, y] : kEndpointGuardReproductionWalls) {
      accepted += model.queue_wall(x, y) ? 1 : 0;
    }
    return accepted;
  }
  if (scenario == "goal-wall") {
    for (auto y = 0; y < 96; ++y) {
      accepted += model.queue_wall(kWidth - 19, y) ? 1 : 0;
    }
    return accepted;
  }
  for (auto y = 0; y < kHeight; ++y) {
    const auto wall = scenario == "tip" ? y >= 32
                      : scenario == "two-gates"
                          ? !((y >= 24 && y < 32) || (y >= 96 && y < 104))
                      : scenario == "four-gates"
                          ? !((y >= 16 && y < 24) || (y >= 48 && y < 56) ||
                              (y >= 80 && y < 88) || (y >= 112 && y < 120))
                          : false;
    if (wall) {
      accepted += model.queue_wall(64, y) ? 1 : 0;
    }
  }
  return accepted;
}

[[nodiscard]] auto run_native_scenario(const NativeScenarioOptions& options)
    -> int {
  ColonyModel model{options.agents};
  model.set_spread_congested_routes(options.spread);
  auto waits = std::uint64_t{0};
  auto one_progress_ticks = std::uint64_t{0};
  auto one_progress_streak = std::uint64_t{0};
  auto longest_one_progress = std::uint64_t{0};
  auto max_pending = model.planning_pending();
  auto ticks = 0;
  auto incremental_wall = std::size_t{0};
  auto accepted_walls = std::size_t{0};
  const auto incremental_walls_pending = [&]() {
    return options.scenario == "browser-incremental" &&
           incremental_wall < std::size(kEndpointGuardReproductionWalls);
  };
  const auto started = std::chrono::steady_clock::now();
  for (; ticks < options.max_ticks &&
         (!model.turnaround_ready() || incremental_walls_pending());
       ++ticks) {
    if (ticks == 4 && options.scenario != "browser-incremental") {
      accepted_walls += queue_native_scenario_walls(model, options.scenario);
    }
    if (ticks >= 4 && incremental_walls_pending()) {
      constexpr auto kWallsPerTick = std::size_t{4};
      auto accepted_this_tick = std::size_t{0};
      while (accepted_this_tick < kWallsPerTick &&
             incremental_walls_pending()) {
        const auto [x, y] = kEndpointGuardReproductionWalls[incremental_wall];
        if (!model.queue_wall(x, y)) {
          break;
        }
        ++accepted_this_tick;
        ++accepted_walls;
        ++incremental_wall;
      }
    }
    (void)model.tick(0.05);
    waits += static_cast<std::uint64_t>(model.movement_waits_last_tick());
    max_pending = std::max(max_pending, model.planning_pending());
    const auto routed_contention =
        model.planning_pending() == 0 && model.movement_waits_last_tick() > 0;
    if (routed_contention && model.advanced_last_tick() == 1) {
      ++one_progress_ticks;
      ++one_progress_streak;
      longest_one_progress =
          std::max(longest_one_progress, one_progress_streak);
    } else {
      one_progress_streak = 0;
    }
  }
  const auto elapsed = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - started)
                           .count();
  const auto& impl = ColonyModelNativeAccess::impl(model);
  const auto walls_complete =
      options.scenario != "browser-incremental" || !incremental_walls_pending();
  const auto complete = walls_complete && model.turnaround_ready() &&
                        model.arrived() == options.agents &&
                        model.crowd_blocked() == 0 && model.unreachable() == 0;
  std::cout << "scenario=" << options.scenario
            << " mode=" << (options.spread ? "spread" : "canonical")
            << " agents=" << options.agents << " ticks=" << ticks
            << " waits=" << waits << " arrived=" << model.arrived()
            << " crowd=" << model.crowd_blocked()
            << " unreachable=" << model.unreachable()
            << " pending=" << model.planning_pending()
            << " complete=" << (complete ? 1 : 0)
            << " walls_accepted=" << accepted_walls
            << " max_pending=" << max_pending
            << " max_planning_queries=" << impl.max_planning_queries
            << " diversity_waves=" << impl.diversity_replan_waves
            << " one_progress_ticks=" << one_progress_ticks
            << " longest_one_progress=" << longest_one_progress
            << " elapsed_ms=" << elapsed << '\n';
  return options.require_complete && !complete ? 1 : 0;
}

}  // namespace

auto run_native_self_check() -> int {
  std::unique_ptr<ColonyModel> demo;
  const auto reset = [&](int agent_count) {
    const auto actual = std::clamp(agent_count, 1, kMaxAgents);
    demo = std::make_unique<ColonyModel>(actual);
    return actual;
  };
  const auto impl = [&]() -> auto& {
    return ColonyModelNativeAccess::impl(*demo);
  };
#if TESS_HAS_EXCEPTIONS
  try {
#endif
    // Endpoint parking must remain structurally traversable after every other
    // colonist settles. Unique equal-length goals are not enough: a complete
    // populated column becomes a wall unless the layout preserves a cross-cut.
    reset(kMaxAgents);
    std::array<std::uint8_t, static_cast<std::size_t>(kWidth) * kHeight>
        home_claims{};
    std::array<std::uint8_t, static_cast<std::size_t>(kWidth) * kHeight>
        away_claims{};
    for (std::size_t i = 0; i < impl().agents.size(); ++i) {
      const auto home = home_tile(i);
      const auto away = away_tile(i);
      const auto home_key = static_cast<std::size_t>(home.y) * kWidth +
                            static_cast<std::size_t>(home.x);
      const auto away_key = static_cast<std::size_t>(away.y) * kWidth +
                            static_cast<std::size_t>(away.x);
      if (home_claims[home_key] != 0 || away_claims[away_key] != 0 ||
          std::abs(away.x - home.x) + std::abs(away.y - home.y) != 109) {
        std::cerr << "web colony model: endpoint parking contract diverged\n";
        return 1;
      }
      home_claims[home_key] = 1;
      away_claims[away_key] = 1;
      if (i != 0) {
        impl().world.field<SettledTag>(away) = true;
      }
    }
    tess::PathScratch endpoint_scratch;
    const auto delayed_endpoint_route = tess::astar_path<World, Traveler>(
        impl().world, {{kWallMaxX + 1, 0, 0}, away_tile(0)}, endpoint_scratch);
    if (delayed_endpoint_route.status != tess::PathStatus::Found) {
      std::cerr << "web colony model: endpoint parking sealed its cross-cut\n";
      return 1;
    }
    constexpr auto kDelayedHomeAgent = std::size_t{7} * kHeight;
    for (std::size_t i = 0; i < impl().agents.size(); ++i) {
      impl().world.field<SettledTag>(away_tile(i)) = false;
      if (i != kDelayedHomeAgent) {
        impl().world.field<SettledTag>(home_tile(i)) = true;
      }
    }
    endpoint_scratch.clear();
    const auto delayed_home_route = tess::astar_path<World, Traveler>(
        impl().world, {{kWallMinX, 0, 0}, home_tile(kDelayedHomeAgent)},
        endpoint_scratch);
    if (delayed_home_route.status != tess::PathStatus::Found) {
      std::cerr << "web colony model: home parking sealed its cross-cut\n";
      return 1;
    }

    // Presentation regression: reset collapses both endpoints, a zero-tick
    // frame changes only alpha, one tick exposes its integer endpoints, and a
    // catch-up frame retains the final tick pair rather than one long jump.
    reset(1);
    if (demo->planning_pending() != 1 || demo->advanced_last_tick() != 0 ||
        demo->movement_waits_last_tick() != 0) {
      std::cerr << "web colony model: reset progress metrics diverged\n";
      return 1;
    }
    const auto initial_x = demo->current_agents()[0];
    const auto initial_y = demo->current_agents()[1];
    if (demo->previous_agents()[0] != initial_x ||
        demo->previous_agents()[1] != initial_y) {
      std::cerr << "web colony model: reset animation pair diverged\n";
      return 1;
    }

    if (demo->tick(0.01) >= 0.0 || demo->current_agents()[0] != initial_x ||
        demo->current_agents()[1] != initial_y ||
        demo->previous_agents()[0] != initial_x ||
        demo->previous_agents()[1] != initial_y ||
        demo->interpolation_alpha() <= 0.0 ||
        demo->interpolation_alpha() >= 1.0) {
      std::cerr << "web colony model: zero-tick animation state changed\n";
      return 1;
    }
    if (demo->tick(0.04) < 0.0 || demo->previous_agents()[0] != initial_x ||
        demo->previous_agents()[1] != initial_y ||
        (demo->current_agents()[0] == initial_x &&
         demo->current_agents()[1] == initial_y) ||
        demo->planning_pending() != 0 || demo->advanced_last_tick() != 1 ||
        demo->movement_waits_last_tick() != 0) {
      std::cerr << "web colony model: one-tick animation pair missing\n";
      return 1;
    }
    const auto before_catch_up_x = demo->current_agents()[0];
    const auto before_catch_up_y = demo->current_agents()[1];
    if (demo->tick(0.10) < 0.0 ||
        (demo->previous_agents()[0] == before_catch_up_x &&
         demo->previous_agents()[1] == before_catch_up_y) ||
        std::abs(demo->current_agents()[0] - demo->previous_agents()[0]) +
                std::abs(demo->current_agents()[1] -
                         demo->previous_agents()[1]) >
            1) {
      std::cerr << "web colony model: catch-up animation pair is not local\n";
      return 1;
    }

    // Swap presentation regression: both logical agents exchange adjacent
    // tiles in one fixed tick, while the renderer receives the exact shared
    // previous/current endpoint pair rather than fractional simulation state.
    reset(2);
    constexpr auto swap_left = tess::Coord3{63, 64, 0};
    constexpr auto swap_right = tess::Coord3{64, 64, 0};
    for (auto& agent : impl().agents) {
      impl().world.field<OccupancyTag>(agent.position) = false;
    }
    impl().agents[0].position = swap_left;
    impl().agents[1].position = swap_right;
    impl().world.field<OccupancyTag>(swap_left) = true;
    impl().world.field<OccupancyTag>(swap_right) = true;
    tess::set_path_agent_goal(impl().tick_state, impl().agents[0], swap_right);
    tess::set_path_agent_goal(impl().tick_state, impl().agents[1], swap_left);
    impl().agents[0].phase = tess::PathAgentPhase::Blocked;
    impl().agents[1].phase = tess::PathAgentPhase::Blocked;
    impl().replan_queue.clear();
    impl().replan_queue.request_all(impl().agents);
    impl().snapshot_after_movement();
    impl().snapshot_before_movement();
    if (demo->tick(0.05) < 0.0 || demo->previous_agents()[0] != swap_left.x ||
        demo->previous_agents()[1] != swap_left.y ||
        demo->previous_agents()[2] != swap_right.x ||
        demo->previous_agents()[3] != swap_right.y ||
        demo->current_agents()[0] != swap_right.x ||
        demo->current_agents()[1] != swap_right.y ||
        demo->current_agents()[2] != swap_left.x ||
        demo->current_agents()[3] != swap_left.y) {
      std::cerr << "web colony model: swap animation pair diverged\n";
      return 1;
    }

    // A browser wall request is an admission decision, not permission to
    // corrupt the movement state. Painting the occupied tile under a moving
    // agent must be rejected synchronously; after the agent vacates it, the
    // same tile is a valid construction target.
    reset(1);
    (void)demo->tick(0.10);
    const auto occupied_tile = impl().agents[0].position;
    if (demo->queue_wall(static_cast<int>(occupied_tile.x),
                         static_cast<int>(occupied_tile.y))) {
      std::cerr << "web colony model: occupied wall request was accepted\n";
      return 1;
    }

    // A rejected incremental stream cannot be healed by ignoring the gap.
    // Poison both the collector and one shadow tile, then prove the baseline
    // restores authoritative content as well as advancing the version chain.
    const auto version_before_gap = impl().version.value;
    const auto shadow_index =
        static_cast<std::size_t>(occupied_tile.y) * kWidth +
        static_cast<std::size_t>(occupied_tile.x);
    impl().shadow[shadow_index] = 1;
    impl().deltas.clear();
    impl().publish_render_frame();
    if (impl().version.value <= version_before_gap ||
        impl().version != impl().deltas.version() ||
        impl().shadow[shadow_index] != 0) {
      std::cerr << "web colony model: rejected delta did not resync\n";
      return 1;
    }
    (void)demo->tick(0.25);
    if (!impl().world.field<PassableTag>(occupied_tile) ||
        impl().world.field<ConstructionTag>(occupied_tile)) {
      std::cerr << "web colony model: rejected wall changed the world\n";
      return 1;
    }
    if (demo->queue_wall(static_cast<int>(occupied_tile.x),
                         static_cast<int>(occupied_tile.y)) != 1) {
      std::cerr << "web colony model: vacated wall request was rejected\n";
      return 1;
    }
    (void)demo->tick(0.05);
    if (impl().world.field<PassableTag>(occupied_tile) ||
        !impl().world.field<ConstructionTag>(occupied_tile)) {
      std::cerr << "web colony model: accepted wall was not built\n";
      return 1;
    }

    // Low-level invariant check: if a caller bypasses wall admission and makes
    // an occupied source impassable, exact pathing still rejects InvalidStart.
    // That library contract is distinct from the demo policy preventing the
    // invalid state in the first place.
    reset(1);
    auto& invalid_start_agent = impl().agents[0];
    impl().world.field<PassableTag>(invalid_start_agent.position) = false;
    invalid_start_agent.phase = tess::PathAgentPhase::Blocked;
    invalid_start_agent.status = tess::PathStatus::Found;
    impl().recover_blocked_agents(1, 1);
    const auto invalid_start_first_queries =
        impl().recover_blocked_agents(1 + kRecoveryWindowTicks, 1);
    if (invalid_start_first_queries != 1 ||
        impl().terrain_confirmation_pending[0] == 0) {
      std::cerr << "web colony model: InvalidStart confirmation not deferred\n";
      return 1;
    }
    const auto invalid_start_second_queries =
        impl().recover_blocked_agents(2 + kRecoveryWindowTicks, 1);
    if (invalid_start_agent.phase != tess::PathAgentPhase::Unreachable ||
        invalid_start_agent.status != tess::PathStatus::InvalidStart ||
        invalid_start_second_queries != 1 ||
        impl().terrain_confirmation_pending[0] != 0) {
      std::cerr << "web colony model: invalid start retried indefinitely\n";
      return 1;
    }

    // Regression: four completed agents can encircle an unoccupied goal.
    // Terrain still has a route, so a Traveler-only NoPath is temporary -- it
    // is evidence that settled teammates must eventually wake, not permission
    // to mark the remaining agent durably blocked.
    reset(5);
    for (auto& agent : impl().agents) {
      impl().world.field<OccupancyTag>(agent.position) = false;
      tess::clear_path_agent_goal(agent);
    }
    tess::diagnostics::FlowAccounting enclosure_flow;
    impl().tick_state.flow_accounting = &enclosure_flow;
    constexpr auto enclosed_goal = tess::Coord3{64, 64, 0};
    constexpr tess::Coord3 positions[] = {
        {62, 64, 0}, {63, 64, 0}, {65, 64, 0}, {64, 63, 0}, {64, 65, 0}};
    for (std::size_t i = 0; i < impl().agents.size(); ++i) {
      impl().agents[i].position = positions[i];
      impl().world.field<OccupancyTag>(positions[i]) = true;
      if (i == 0) {
        tess::set_path_agent_goal(impl().tick_state, impl().agents[i],
                                  enclosed_goal);
        impl().agents[i].phase = tess::PathAgentPhase::Blocked;
        impl().agents[i].status = tess::PathStatus::Found;
      }
    }
    impl().sync_settled_obstacles();
    impl().recover_blocked_agents(1, 1);
    const auto enclosure_first_queries =
        impl().recover_blocked_agents(1 + kRecoveryWindowTicks, 1);
    if (enclosure_first_queries != 1 ||
        impl().terrain_confirmation_pending[0] == 0) {
      std::cerr << "web colony model: enclosure confirmation not deferred\n";
      return 1;
    }
    const auto enclosure_second_queries =
        impl().recover_blocked_agents(2 + kRecoveryWindowTicks, 1);
    if (impl().unreachable() != 0 || impl().crowd_blocked_count() != 1 ||
        !impl().turnaround_ready() || enclosure_second_queries != 1 ||
        impl().terrain_confirmation_pending[0] != 0 ||
        !enclosure_flow.counters.retention_identity_holds() ||
        enclosure_flow.counters.cancelled != 1 ||
        enclosure_flow.counters.outstanding_current != 0) {
      std::cerr << "web colony model: settled enclosure misclassified\n";
      return 1;
    }

    if (impl().relaunch() != 2 || impl().completed_leg_count() != 0 ||
        impl().aborted_leg_count() != 1 ||
        !enclosure_flow.counters.retention_identity_holds() ||
        enclosure_flow.counters.outstanding_current != 5) {
      std::cerr << "web colony model: settled wave did not turn around\n";
      return 1;
    }
    if (!impl().recovery_schedule.due_agent_indices().empty()) {
      std::cerr << "web colony model: turnaround retained recovery state\n";
      return 1;
    }
    for (int frame = 0; frame < 1000 && !impl().turnaround_ready(); ++frame) {
      (void)demo->tick(0.05);
    }
    if (impl().arrived() != 5 || impl().crowd_blocked_count() != 0 ||
        impl().unreachable() != 0 ||
        !enclosure_flow.counters.retention_identity_holds() ||
        enclosure_flow.counters.completed != 5 ||
        enclosure_flow.counters.outstanding_current != 0) {
      std::cerr << "web colony model: post-enclosure leg did not complete\n";
      return 1;
    }
    if (impl().relaunch() != 3 || impl().completed_leg_count() != 1 ||
        impl().aborted_leg_count() != 1 ||
        !enclosure_flow.counters.retention_identity_holds() ||
        enclosure_flow.counters.outstanding_current != 5) {
      std::cerr << "web colony model: post-enclosure accounting diverged\n";
      return 1;
    }

    // An aisle beside a populated endpoint column is not a cross-cut through
    // that column. Model a delayed agent behind a fully settled cohort and
    // prove the recovery classifier treats the resulting Traveler-only seal
    // as crowd blocking, not durable terrain failure.
    reset(1);
    auto& delayed_agent = impl().agents[0];
    impl().world.field<OccupancyTag>(delayed_agent.position) = false;
    delayed_agent.position = {110, 64, 0};
    impl().world.field<OccupancyTag>(delayed_agent.position) = true;
    tess::set_path_agent_goal(impl().tick_state, delayed_agent, {112, 64, 0});
    delayed_agent.phase = tess::PathAgentPhase::Blocked;
    delayed_agent.status = tess::PathStatus::Found;
    for (int y = 0; y < kHeight; ++y) {
      impl().world.field<SettledTag>({111, y, 0}) = true;
    }
    impl().recover_blocked_agents(1, 1);
    const auto settled_column_first_queries =
        impl().recover_blocked_agents(1 + kRecoveryWindowTicks, 1);
    const auto settled_column_second_queries =
        impl().recover_blocked_agents(2 + kRecoveryWindowTicks, 1);
    if (settled_column_first_queries != 1 ||
        settled_column_second_queries != 1 || impl().unreachable() != 0 ||
        impl().crowd_blocked_count() != 1 || !impl().turnaround_ready()) {
      std::cerr << "web colony model: settled endpoint column misclassified\n";
      return 1;
    }

    // The all-agent-replan comparison mode uses the same demo-owned outcome
    // classifier. Exercise its reachable-terrain path, not only a graph-pruned
    // wall seal.
    reset(5);
    for (auto& agent : impl().agents) {
      impl().world.field<OccupancyTag>(agent.position) = false;
      tess::clear_path_agent_goal(agent);
    }
    for (std::size_t i = 0; i < impl().agents.size(); ++i) {
      impl().agents[i].position = positions[i];
      impl().world.field<OccupancyTag>(positions[i]) = true;
      if (i == 0) {
        tess::set_path_agent_goal(impl().tick_state, impl().agents[i],
                                  enclosed_goal);
        impl().agents[i].phase = tess::PathAgentPhase::Blocked;
        impl().agents[i].status = tess::PathStatus::Found;
      }
    }
    demo->set_replan_each_tick(1);
    for (int frame = 0; frame < 1000 && !impl().turnaround_ready(); ++frame) {
      (void)demo->tick(0.05);
    }
    if (impl().arrived() != 4 || impl().crowd_blocked_count() != 1 ||
        impl().unreachable() != 0 || impl().relaunch() != 2) {
      std::cerr << "web colony model: replan mode misclassified enclosure\n";
      return 1;
    }
    for (int frame = 0; frame < 1000 && !impl().turnaround_ready(); ++frame) {
      (void)demo->tick(0.05);
    }
    if (impl().arrived() != 5 || impl().crowd_blocked_count() != 0 ||
        impl().unreachable() != 0) {
      std::cerr << "web colony model: replan enclosure recovery failed\n";
      return 1;
    }

    reset(8);
    for (int frame = 0; frame < 5000 && demo->arrived() < 8; ++frame) {
      if (frame == 4) {
        for (int y = 0; y < kHeight - 8; ++y) {
          (void)demo->queue_wall(64, y);
        }
      }
      (void)demo->tick(0.05);
    }
    if (demo->arrived() != 8) {
      std::cerr << "web colony model: agents did not arrive\n";
      return 1;
    }
    if (impl().built_tiles != static_cast<std::size_t>(kHeight - 8)) {
      std::cerr << "web colony model: wall not built\n";
      return 1;
    }
    const auto* tiles = demo->tiles();
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
    reset(kBottleneckAgents);
    for (int frame = 0; frame < 400; ++frame) {
      (void)demo->tick(0.05);
      if (impl().turnaround_ready()) {
        (void)impl().relaunch();
      }
    }
    for (int y = 0; y <= 74; ++y) {
      (void)demo->queue_wall(60, y);
    }
    for (int y = 63; y < kHeight; ++y) {
      (void)demo->queue_wall(48, y);
    }
    const auto bottleneck_start_leg = impl().current_leg();
    const auto bottleneck_start_completed = impl().completed_leg_count();
    for (int frame = 0;
         frame < 4000 && impl().current_leg() < bottleneck_start_leg + 2;
         ++frame) {
      (void)demo->tick(0.05);
      if (impl().turnaround_ready()) {
        (void)impl().relaunch();
      }
    }
    if (demo->unreachable() != 0) {
      std::cerr << "web colony model: " << demo->unreachable()
                << " agents wedged behind the bottleneck\n";
      return 1;
    }
    if (impl().current_leg() < bottleneck_start_leg + 2 ||
        impl().completed_leg_count() <= bottleneck_start_completed) {
      std::cerr << "web colony model: convoy stalled at the bottleneck ("
                << demo->arrived() << "/" << kBottleneckAgents << " arrived)\n";
      return 1;
    }
    if (impl().max_recovery_probes > kRecoveryOptions.max_probes_per_tick) {
      std::cerr << "web colony model: recovery probe budget exceeded ("
                << impl().max_recovery_probes << "/"
                << kRecoveryOptions.max_probes_per_tick << ")\n";
      return 1;
    }
    if (impl().max_planning_queries > kMaxPlanningQueriesPerTick) {
      std::cerr << "web colony model: shared planning budget exceeded ("
                << impl().max_planning_queries << "/"
                << kMaxPlanningQueriesPerTick << ")\n";
      return 1;
    }

    // Accumulated walls behind a cohort cannot decide whether a current
    // start-to-goal barrier already supplies enough openings. Give an older
    // four-gate column and the active wall equal construction density; only
    // the one-gate column ahead is straddled by this outbound cohort.
    reset(64);
    for (std::size_t i = 0; i < impl().agents.size(); ++i) {
      impl().agents[i].position =
          tess::Coord3{80, static_cast<std::int64_t>(i), 0};
      impl().agents[i].goal =
          tess::Coord3{120, static_cast<std::int64_t>(i), 0};
    }
    for (auto y = 0; y < kHeight; ++y) {
      const auto old_wall = !((y >= 16 && y < 24) || (y >= 48 && y < 56) ||
                              (y >= 80 && y < 88) || (y >= 112 && y < 120));
      impl().world.field<ConstructionTag>(tess::Coord3{40, y, 0}) = old_wall;
      impl().world.field<ConstructionTag>(tess::Coord3{90, y, 0}) = y >= 32;
    }
    if (impl().dominant_barrier_open_runs() != 1) {
      std::cerr << "web colony model: stale wall selected as active barrier\n";
      return 1;
    }

    // Optional congestion spreading waits for measured route contention,
    // then performs one bounded seeded replan wave. A central two-gate wall
    // exercises the activation path without entering either guarded endpoint
    // approach.
    reset(kBottleneckAgents);
    demo->set_spread_congested_routes(true);
    for (int frame = 0; frame < 1000 && !impl().turnaround_ready(); ++frame) {
      if (frame == 4) {
        // Sparse wall crossings inside both endpoint-approach guards are not
        // the congesting barrier and must not suppress central spreading.
        (void)demo->queue_wall(kWallMinX, 0);
        (void)demo->queue_wall(kWallMaxX, kHeight - 1);
        for (int y = 0; y < kHeight; ++y) {
          if (!((y >= 24 && y < 32) || (y >= 96 && y < 104))) {
            (void)demo->queue_wall(64, y);
          }
        }
      }
      (void)demo->tick(0.05);
    }
    if (!impl().turnaround_ready() || impl().arrived() != kBottleneckAgents ||
        impl().crowd_blocked_count() != 0 || impl().unreachable() != 0 ||
        impl().diversity_replan_waves != 1 ||
        impl().left_approach_wall_tiles != 1 ||
        impl().right_approach_wall_tiles != 1) {
      std::cerr << "web colony model: congested route spreading failed\n";
      return 1;
    }

    // Pin the documented endpoint-approach boundary and prove repeated wall
    // submissions do not inflate the count of distinct construction tiles.
    reset(1);
    demo->set_spread_congested_routes(true);
    for (auto y = std::size_t{0}; y < kApproachBarrierTiles - 1; ++y) {
      (void)demo->queue_wall(kWallMinX, static_cast<int>(y));
      (void)demo->queue_wall(kWallMinX, static_cast<int>(y));
    }
    (void)demo->tick(0.05);
    if (impl().left_approach_wall_tiles != kApproachBarrierTiles - 1 ||
        impl().endpoint_approach_obstructed()) {
      std::cerr << "web colony model: 63-tile endpoint guard diverged\n";
      return 1;
    }
    const auto boundary_y = static_cast<int>(kApproachBarrierTiles - 1);
    (void)demo->queue_wall(kWallMinX, boundary_y);
    (void)demo->queue_wall(kWallMinX, boundary_y);
    (void)demo->tick(0.05);
    if (impl().left_approach_wall_tiles != kApproachBarrierTiles ||
        !impl().endpoint_approach_obstructed()) {
      std::cerr << "web colony model: 64-tile endpoint guard diverged\n";
      return 1;
    }

    // A single seeded snapshot still leaves a wide merge at a long wall tip.
    // Once at least 64 distinct next tiles have competing claims, one
    // additional bounded seed must disperse the merge without changing the
    // terminal outcome.
    constexpr int kWideMergeAgents = 640;
    reset(kWideMergeAgents);
    demo->set_spread_congested_routes(true);
    for (int frame = 0; frame < 1000 && !impl().turnaround_ready(); ++frame) {
      if (frame == 4) {
        for (int y = 32; y < kHeight; ++y) {
          (void)demo->queue_wall(64, y);
        }
      }
      (void)demo->tick(0.05);
    }
    if (!impl().turnaround_ready() || impl().arrived() != kWideMergeAgents ||
        impl().crowd_blocked_count() != 0 || impl().unreachable() != 0 ||
        impl().wide_merge_replan_waves != 1 ||
        impl().max_planning_queries > kMaxPlanningQueriesPerTick) {
      std::cerr << "web colony model: wide wall-tip merge did not disperse\n";
      return 1;
    }

    // Gate queues can eventually produce many blocked agents, but their first
    // post-wave snapshot is not the broad wall-tip merge above. Prove that the
    // one-shot observation rejects this scale-matched control and cannot
    // trigger a later second wave as contention evolves.
    constexpr int kWideMergeControlAgents = 640;
    reset(kWideMergeControlAgents);
    demo->set_spread_congested_routes(true);
    for (int frame = 0; frame < 1000 && !impl().turnaround_ready(); ++frame) {
      if (frame == 4) {
        for (int y = 0; y < kHeight; ++y) {
          if (!((y >= 24 && y < 32) || (y >= 96 && y < 104))) {
            (void)demo->queue_wall(64, y);
          }
        }
      }
      (void)demo->tick(0.05);
    }
    if (!impl().turnaround_ready() ||
        impl().arrived() != kWideMergeControlAgents ||
        impl().crowd_blocked_count() != 0 || impl().unreachable() != 0 ||
        impl().wide_merge_replan_waves != 0 || !impl().wide_merge_checked) {
      std::cerr << "web colony model: ordinary gate triggered wide merge\n";
      return 1;
    }

    constexpr int kDiversityContractAgents = 64;

    // A topology edit can land while the seeded snapshot is only partly
    // drained. After one planning batch, prove the topology owner cancels the
    // remaining snapshot and canonicalizes every active agent.
    reset(kDiversityContractAgents);
    demo->set_spread_congested_routes(true);
    impl().last_movement_waits = 8;
    impl().update_route_diversity(kTopologyIdleTicks);
    (void)tess::process_weighted_path_agent_replans<World, Traveler>(
        impl().world, impl().agents, impl().tick_state.routes,
        impl().diversity_replan_queue, impl().replan_scratch,
        tess::PathAgentReplanOptions{
            .max_requests = kMaxPlanningQueriesPerTick,
            .equal_cost_tie_seed = 0x434f4c4f4e59ULL,
        });
    if (impl().diversity_replan_queue.pending() !=
        kDiversityContractAgents - kMaxPlanningQueriesPerTick) {
      std::cerr << "web colony model: seeded replan batch was not bounded\n";
      return 1;
    }
    constexpr auto in_wave_wall = tess::Coord3{64, 63, 0};
    impl().world.field<PassableTag>(in_wave_wall) = false;
    impl().world.field<ConstructionTag>(in_wave_wall) = true;
    const auto in_wave_wall_key =
        tess::chunk_key<Shape>(tess::chunk_coord<Shape>(in_wave_wall));
    impl().world.mark_topology_dirty(
        in_wave_wall_key, kTerrainDirty,
        tess::Box3{in_wave_wall, tess::Extent3{1, 1, 1}});
    constexpr auto kInWaveEditTick = std::uint64_t{100};
    (void)impl().topology_task(
        tess::ScheduleTaskContext{tess::SimClock{kInWaveEditTick}});
    if (!impl().diversity_replan_queue.empty() ||
        impl().replan_queue.pending() != kDiversityContractAgents ||
        impl().routes_diversified || impl().diversity_wave_attempted ||
        impl().last_topology_edit_tick != kInWaveEditTick) {
      std::cerr << "web colony model: topology retained seeded replan work\n";
      return 1;
    }
    while (!impl().replan_queue.empty()) {
      (void)tess::process_weighted_path_agent_replans<World, Traveler>(
          impl().world, impl().agents, impl().tick_state.routes,
          impl().replan_queue, impl().replan_scratch,
          tess::PathAgentReplanOptions{
              .max_requests = kMaxPlanningQueriesPerTick,
          });
    }
    tess::PathScratch in_wave_canonical_scratch;
    const auto in_wave_canonical = tess::weighted_astar_path<World, Traveler>(
        impl().world,
        tess::PathRequest{impl().agents[0].position, impl().agents[0].goal},
        in_wave_canonical_scratch);
    if (in_wave_canonical.status != tess::PathStatus::Found ||
        impl().tick_state.routes.routes[0] !=
            std::vector<tess::Coord3>(in_wave_canonical.path.begin(),
                                      in_wave_canonical.path.end())) {
      std::cerr << "web colony model: in-wave edit was not canonicalized\n";
      return 1;
    }
    impl().last_movement_waits = 8;
    impl().update_route_diversity(kInWaveEditTick + kTopologyIdleTicks - 1);
    if (!impl().diversity_replan_queue.empty() ||
        impl().diversity_replan_waves != 1) {
      std::cerr << "web colony model: topology idle gate opened early\n";
      return 1;
    }
    impl().update_route_diversity(kInWaveEditTick + kTopologyIdleTicks);
    if (impl().diversity_replan_queue.pending() != kDiversityContractAgents ||
        !impl().routes_diversified || impl().diversity_replan_waves != 2) {
      std::cerr << "web colony model: stable topology did not re-seed\n";
      return 1;
    }

    // The same topology authority applies to the optional second seed. Cancel
    // it after one batch and return every active agent to canonical planning.
    reset(kDiversityContractAgents);
    demo->set_spread_congested_routes(true);
    impl().routes_diversified = true;
    impl().wide_merge_checked = true;
    impl().wide_merge_replan_queue.request_all(impl().agents);
    (void)tess::process_weighted_path_agent_replans<World, Traveler>(
        impl().world, impl().agents, impl().tick_state.routes,
        impl().wide_merge_replan_queue, impl().replan_scratch,
        tess::PathAgentReplanOptions{
            .max_requests = kMaxPlanningQueriesPerTick,
            .equal_cost_tie_seed = 0x434f4e47455354ULL,
        });
    if (impl().wide_merge_replan_queue.pending() !=
        kDiversityContractAgents - kMaxPlanningQueriesPerTick) {
      std::cerr << "web colony model: wide-merge batch was not bounded\n";
      return 1;
    }
    constexpr auto in_second_wave_wall = tess::Coord3{64, 62, 0};
    impl().world.field<PassableTag>(in_second_wave_wall) = false;
    impl().world.field<ConstructionTag>(in_second_wave_wall) = true;
    const auto in_second_wave_wall_key =
        tess::chunk_key<Shape>(tess::chunk_coord<Shape>(in_second_wave_wall));
    impl().world.mark_topology_dirty(
        in_second_wave_wall_key, kTerrainDirty,
        tess::Box3{in_second_wave_wall, tess::Extent3{1, 1, 1}});
    (void)impl().topology_task(tess::ScheduleTaskContext{tess::SimClock{200}});
    if (!impl().wide_merge_replan_queue.empty() ||
        impl().replan_queue.pending() != kDiversityContractAgents ||
        impl().routes_diversified || impl().diversity_wave_attempted ||
        impl().wide_merge_checked) {
      std::cerr << "web colony model: topology retained wide-merge work\n";
      return 1;
    }

    // A fully drained seed has the same topology lifetime. A later edit first
    // restores canonical routes, then becomes eligible for one fresh bounded
    // snapshot only after the edit-idle interval.
    reset(kDiversityContractAgents);
    demo->set_spread_congested_routes(true);
    impl().last_movement_waits = 8;
    impl().update_route_diversity(kTopologyIdleTicks);
    while (!impl().diversity_replan_queue.empty()) {
      (void)tess::process_weighted_path_agent_replans<World, Traveler>(
          impl().world, impl().agents, impl().tick_state.routes,
          impl().diversity_replan_queue, impl().replan_scratch,
          tess::PathAgentReplanOptions{
              .max_requests = kMaxPlanningQueriesPerTick,
              .equal_cost_tie_seed = 0x434f4c4f4e59ULL,
          });
    }
    constexpr auto later_wall = tess::Coord3{64, 64, 0};
    impl().world.field<PassableTag>(later_wall) = false;
    impl().world.field<ConstructionTag>(later_wall) = true;
    const auto later_wall_key =
        tess::chunk_key<Shape>(tess::chunk_coord<Shape>(later_wall));
    impl().world.mark_topology_dirty(
        later_wall_key, kTerrainDirty,
        tess::Box3{later_wall, tess::Extent3{1, 1, 1}});
    constexpr auto kLaterEditTick = std::uint64_t{300};
    (void)impl().topology_task(
        tess::ScheduleTaskContext{tess::SimClock{kLaterEditTick}});
    while (!impl().replan_queue.empty()) {
      (void)tess::process_weighted_path_agent_replans<World, Traveler>(
          impl().world, impl().agents, impl().tick_state.routes,
          impl().replan_queue, impl().replan_scratch,
          tess::PathAgentReplanOptions{
              .max_requests = kMaxPlanningQueriesPerTick,
          });
    }
    tess::PathScratch canonical_scratch;
    const auto canonical = tess::weighted_astar_path<World, Traveler>(
        impl().world,
        tess::PathRequest{impl().agents[0].position, impl().agents[0].goal},
        canonical_scratch);
    impl().last_movement_waits = 8;
    impl().update_route_diversity(kLaterEditTick + kTopologyIdleTicks - 1);
    if (canonical.status != tess::PathStatus::Found ||
        impl().tick_state.routes.routes[0] !=
            std::vector<tess::Coord3>(canonical.path.begin(),
                                      canonical.path.end()) ||
        impl().routes_diversified || !impl().diversity_replan_queue.empty() ||
        impl().diversity_replan_waves != 1) {
      std::cerr << "web colony model: later edit was not canonicalized\n";
      return 1;
    }
    impl().update_route_diversity(kLaterEditTick + kTopologyIdleTicks);
    if (!impl().routes_diversified ||
        impl().diversity_replan_queue.pending() != kDiversityContractAgents ||
        impl().diversity_replan_waves != 2) {
      std::cerr << "web colony model: later stable edit did not re-seed\n";
      return 1;
    }

    // All eight destination columns remain a normal successful case when no
    // obstacle disturbs their equal-length routes. Exercise both directions
    // at maximum population before injecting the reported partial outcome.
    reset(kMaxAgents);
    for (int frame = 0; frame < 1200 && impl().current_leg() < 3; ++frame) {
      (void)demo->tick(0.05);
      if (impl().turnaround_ready()) {
        (void)impl().relaunch();
      }
    }
    if (impl().current_leg() < 3 || impl().completed_leg_count() != 2 ||
        impl().aborted_leg_count() != 0 || impl().unreachable() != 0 ||
        impl().crowd_blocked_count() != 0) {
      std::cerr << "web colony model: open maximum-scale waves failed\n";
      return 1;
    }

    // A wall just before the endpoint band funnels every upper-row agent
    // through one gap. Permanent aisle columns keep early settlers from
    // sealing the goals behind them, so maximum-scale travel still completes.
    reset(kMaxAgents);
    demo->set_spread_congested_routes(true);
    for (int frame = 0; frame < 5000 && !impl().turnaround_ready(); ++frame) {
      if (frame == 4) {
        for (int y = 0; y < 96; ++y) {
          (void)demo->queue_wall(kWallMaxX, y);
        }
      }
      (void)demo->tick(0.05);
    }
    if (!impl().turnaround_ready() || impl().arrived() != kMaxAgents ||
        impl().crowd_blocked_count() != 0 || impl().unreachable() != 0 ||
        impl().relaunch() != 2 || impl().completed_leg_count() != 1 ||
        impl().aborted_leg_count() != 0 || impl().diversity_replan_waves != 0) {
      std::cerr << "web colony model: aisled outbound goals did not complete\n";
      return 1;
    }
    for (int frame = 0; frame < 5000 && !impl().turnaround_ready(); ++frame) {
      (void)demo->tick(0.05);
    }
    if (!impl().turnaround_ready() || impl().arrived() != kMaxAgents ||
        impl().crowd_blocked_count() != 0 || impl().unreachable() != 0 ||
        impl().relaunch() != 3 || impl().completed_leg_count() != 2 ||
        impl().aborted_leg_count() != 0 ||
        impl().outstanding_goal_count() != kMaxAgents) {
      std::cerr << "web colony model: aisled inbound goals did not complete\n";
      return 1;
    }

    // Maximum-scale form of the reported state: 968 agents reached their away
    // tiles and 56 were crowd-blocked for this leg. The controller must treat
    // that as a quiescent wave, not 56 durable failures, and rearm all 1,024
    // toward home through the same bounded FIFO.
    reset(kMaxAgents);
    constexpr std::size_t kReportedArrivals = 968;
    for (auto& agent : impl().agents) {
      impl().world.field<OccupancyTag>(agent.position) = false;
    }
    for (std::size_t i = 0; i < impl().agents.size(); ++i) {
      auto& agent = impl().agents[i];
      if (i < kReportedArrivals) {
        agent.position = away_tile(i);
        tess::clear_path_agent_goal(agent);
      } else {
        agent.position = {64, static_cast<std::int64_t>(i - kReportedArrivals),
                          0};
        tess::clear_path_agent_goal(agent);
        impl().crowd_blocked[i] = 1;
      }
      impl().world.field<OccupancyTag>(agent.position) = true;
    }
    if (!impl().turnaround_ready() || impl().arrived() != kReportedArrivals ||
        impl().unreachable() != 0 ||
        impl().crowd_blocked_count() !=
            kMaxAgents - static_cast<int>(kReportedArrivals) ||
        impl().relaunch() != 2 ||
        impl().outstanding_goal_count() != kMaxAgents ||
        impl().completed_leg_count() != 0 || impl().aborted_leg_count() != 1) {
      std::cerr << "web colony model: maximum-scale wave did not rearm\n";
      return 1;
    }
    const auto first_crowd_blocked = kReportedArrivals;
    const auto before_turnaround = impl().agents[first_crowd_blocked].position;
    for (int frame = 0;
         frame < 300 &&
         impl().agents[first_crowd_blocked].position == before_turnaround;
         ++frame) {
      (void)demo->tick(0.25);
    }
    if (impl().agents[first_crowd_blocked].position == before_turnaround ||
        impl().max_planning_queries > kMaxPlanningQueriesPerTick) {
      std::cerr
          << "web colony model: maximum-scale turnaround made no progress\n";
      return 1;
    }
    for (int frame = 0; frame < 2000 && !impl().turnaround_ready(); ++frame) {
      (void)demo->tick(0.05);
    }
    if (impl().arrived() != kMaxAgents || impl().crowd_blocked_count() != 0 ||
        impl().unreachable() != 0 || impl().relaunch() != 3 ||
        impl().completed_leg_count() != 1 || impl().aborted_leg_count() != 1) {
      std::cerr << "web colony model: maximum-scale recovery did not finish\n";
      return 1;
    }

    // The other half of that contract: refusing to cry wolf must not cost the
    // demo its ability to report a real seal. A wall spanning every row leaves
    // no route at all, and the page is expected to say so.
    reset(8);
    for (int y = 0; y < kHeight; ++y) {
      (void)demo->queue_wall(64, y);
    }
    for (int frame = 0; frame < 300; ++frame) {
      (void)demo->tick(0.05);
    }
    if (demo->unreachable() != 8) {
      std::cerr << "web colony model: sealed goals not reported as durable ("
                << demo->unreachable() << "/8)\n";
      return 1;
    }
    if (impl().turnaround_ready() || impl().relaunch() != 1) {
      std::cerr << "web colony model: wall-sealed wave relaunched\n";
      return 1;
    }

    // The intentionally expensive comparison strategy must preserve the same
    // outcome semantics even though it discards retained routes each tick.
    reset(8);
    demo->set_replan_each_tick(1);
    for (int y = 0; y < kHeight; ++y) {
      (void)demo->queue_wall(64, y);
    }
    for (int frame = 0; frame < 300; ++frame) {
      (void)demo->tick(0.05);
    }
    if (demo->unreachable() != 8 || impl().turnaround_ready()) {
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
    reset(kStallAgents);
    for (int frame = 0; frame < 300; ++frame) {
      (void)demo->tick(0.05);
      if (impl().turnaround_ready()) {
        (void)impl().relaunch();
      }
    }
    constexpr int kStallWalls[3][3] = {
        {49, 47, 48}, {24, 10, 12}, {75, 23, 24}};
    for (const auto& spec : kStallWalls) {
      for (int y = 0; y < kHeight; ++y) {
        if (y < spec[1] || y >= spec[2]) {
          (void)demo->queue_wall(spec[0], y);
        }
      }
    }
    const auto stall_start_leg = impl().current_leg();
    const auto stall_start_completed = impl().completed_leg_count();
    for (int frame = 0; frame < 1200; ++frame) {
      (void)demo->tick(0.05);
      if (impl().turnaround_ready()) {
        (void)impl().relaunch();
      }
    }
    if (impl().current_leg() <= stall_start_leg ||
        impl().completed_leg_count() <= stall_start_completed) {
      std::cerr << "web colony model: no full leg completed through the "
                   "bottleneck geometry under joint movement\n";
      return 1;
    }
    if (demo->unreachable() != 0) {
      std::cerr << "web colony model: bottleneck geometry reported "
                << demo->unreachable() << " durable failures\n";
      return 1;
    }
    if (demo->stalled_ticks() >= 100) {
      std::cerr << "web colony model: bottleneck geometry stalled under "
                   "joint movement ("
                << demo->stalled_ticks() << " motionless ticks)\n";
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
  return 0;
}

}  // namespace tess::examples::web_colony

int main(int argc, char** argv) {
  if (argc == 1) {
    return tess::examples::web_colony::run_native_self_check();
  }
  if (argc == 2 && std::string_view{argv[1]} == "--help") {
    tess::examples::web_colony::print_native_scenario_usage();
    return 0;
  }
  auto options = tess::examples::web_colony::NativeScenarioOptions{};
  if (!tess::examples::web_colony::parse_native_scenario_options(argc, argv,
                                                                 options)) {
    tess::examples::web_colony::print_native_scenario_usage();
    return 2;
  }
  return tess::examples::web_colony::run_native_scenario(options);
}

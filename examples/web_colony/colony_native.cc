#include "colony_model_internal.h"

namespace tess::examples::web_colony {

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
    // Presentation regression: reset collapses both endpoints, a zero-tick
    // frame changes only alpha, one tick exposes its integer endpoints, and a
    // catch-up frame retains the final tick pair rather than one long jump.
    reset(1);
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
         demo->current_agents()[1] == initial_y)) {
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
    (void)demo->tick(0.05);
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

    // Natural maximum-scale reproduction: a wall just before the eight goal
    // columns funnels every upper-row agent through one gap. The nearest goal
    // column settles first and cuts off farther columns, producing a
    // settled-only quiescent outcome without synthetic phase edits.
    reset(kMaxAgents);
    for (int frame = 0; frame < 5000 && !impl().turnaround_ready(); ++frame) {
      if (frame == 4) {
        for (int y = 0; y < 96; ++y) {
          (void)demo->queue_wall(kWallMaxX, y);
        }
      }
      (void)demo->tick(0.05);
    }
    if (!impl().turnaround_ready() || impl().arrived() == 0 ||
        impl().crowd_blocked_count() == 0 || impl().unreachable() != 0 ||
        impl().arrived() + impl().crowd_blocked_count() != kMaxAgents ||
        impl().relaunch() != 2 || impl().completed_leg_count() != 0 ||
        impl().aborted_leg_count() != 1) {
      std::cerr << "web colony model: natural settled seal not recovered\n";
      return 1;
    }
    for (int frame = 0; frame < 5000 && !impl().turnaround_ready(); ++frame) {
      (void)demo->tick(0.05);
    }
    if (!impl().turnaround_ready() || impl().arrived() == 0 ||
        impl().crowd_blocked_count() == 0 || impl().unreachable() != 0 ||
        impl().arrived() + impl().crowd_blocked_count() != kMaxAgents ||
        impl().relaunch() != 3 || impl().completed_leg_count() != 0 ||
        impl().aborted_leg_count() != 2 ||
        impl().outstanding_goal_count() != kMaxAgents) {
      std::cerr << "web colony model: repeated settled seal stopped the wave\n";
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

int main() { return tess::examples::web_colony::run_native_self_check(); }

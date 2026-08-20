#include "traffic_model.h"

#include <tess/diagnostics/diagnostics.h>
#include <tess/pathfinding.h>
#include <tess/simulation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace tess::examples::web_traffic {

namespace {

struct PassableTag {};
struct ConstructionTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

constexpr std::size_t kSearchBudget = 8;
constexpr double kFixedStepSeconds = 1.0 / 20.0;
constexpr int kMaxCatchUpTicks = 8;
constexpr int kBarrierMinX = traffic_width / 2 - 2;
constexpr int kBarrierMaxX = traffic_width / 2 + 1;

using Shape = tess::Shape<tess::Extent3{traffic_width, traffic_height, 1},
                          tess::Extent3{32, 32, 1}>;
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<ConstructionTag, bool>,
    tess::Field<CostTag, std::uint32_t>, tess::Field<OccupancyTag, bool>,
    tess::Field<ReservationTag, bool>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
using Driver = tess::movement::MovementClass<
    tess::movement::AllOf<
        tess::movement::Field<PassableTag>,
        tess::movement::Not<tess::movement::Field<ConstructionTag>>>,
    tess::movement::FieldCost<CostTag>>;

constexpr auto permuted_row(int row) noexcept -> int {
  return (row * 73 + 19) % traffic_height;
}

constexpr auto scenario_has_opening(TrafficScenario scenario, int y) -> bool {
  if (scenario == TrafficScenario::Funnel) {
    return y >= traffic_height / 2 - 12 && y < traffic_height / 2 + 12;
  }
  if (scenario == TrafficScenario::MultiGate) {
    const auto within_band = y % 64;
    return within_band >= 28 && within_band < 36;
  }
  return true;
}

constexpr auto uses_guided_routes(TrafficScenario scenario) noexcept -> bool {
  return scenario == TrafficScenario::Funnel ||
         scenario == TrafficScenario::MultiGate;
}

constexpr auto nearest_gate_row(TrafficScenario scenario,
                                std::int64_t row) noexcept -> std::int64_t {
  auto best = std::int64_t{0};
  auto best_distance = std::numeric_limits<std::int64_t>::max();
  for (auto y = std::int64_t{0}; y < traffic_height; ++y) {
    if (!scenario_has_opening(scenario, static_cast<int>(y))) {
      continue;
    }
    const auto distance = std::abs(y - row);
    if (distance < best_distance) {
      best = y;
      best_distance = distance;
    }
  }
  return best;
}

auto guided_route(const World& world, TrafficScenario scenario,
                  tess::PathRequest request, tess::PathScratch& scratch,
                  tess::WeightedPortalRouteProduct& product)
    -> tess::PathResult {
  const auto gate_y = nearest_gate_row(scenario, request.start.y);
  const auto left_to_right = request.start.x < request.goal.x;
  const auto near_x = left_to_right ? kBarrierMinX - 1 : kBarrierMaxX + 1;
  const auto far_x = left_to_right ? kBarrierMaxX + 1 : kBarrierMinX - 1;
  auto waypoints = std::array<tess::Coord3, 3>{};
  auto waypoint_count = std::size_t{0};
  waypoints[waypoint_count++] = {near_x, gate_y, 0};
  waypoints[waypoint_count++] = {far_x, gate_y, 0};
  if (request.goal.y != gate_y) {
    waypoints[waypoint_count++] = {far_x, request.goal.y, 0};
  }
  return tess::build_weighted_portal_route_product<World, PassableTag, CostTag>(
      world, request,
      std::span<const tess::Coord3>{waypoints.data(), waypoint_count}, scratch,
      product);
}

constexpr auto starts_on_left(TrafficScenario scenario, int lane) -> bool {
  return scenario == TrafficScenario::Aligned || lane == 0;
}

constexpr auto start_tile(TrafficScenario scenario, std::size_t index)
    -> tess::Coord3 {
  const auto lane = static_cast<int>(index / traffic_height);
  const auto row = static_cast<int>(index % traffic_height);
  const auto left = starts_on_left(scenario, lane);
  return {left ? 8 + lane * 4 : traffic_width - 9, row, 0};
}

constexpr auto goal_tile(TrafficScenario scenario, std::size_t index)
    -> tess::Coord3 {
  const auto lane = static_cast<int>(index / traffic_height);
  const auto row = static_cast<int>(index % traffic_height);
  const auto goal_row =
      scenario == TrafficScenario::ShuffledCrossing ? permuted_row(row) : row;
  const auto left = starts_on_left(scenario, lane);
  return {left ? traffic_width - 9 - lane * 4 : 8, goal_row, 0};
}

}  // namespace

struct TrafficModel::Impl {
  explicit Impl(TrafficScenario selected) : selected_scenario(selected) {
    initialize_world();
    initialize_scenario();
    initialize_agents();
  }

  World world{};
  std::vector<tess::PathAgentState> agents{};
  tess::PathAgentTickState tick_state{};
  tess::PathAgentReplanQueue replan_queue{};
  tess::PathScratch replan_scratch{};
  tess::WeightedPortalRouteProduct replan_product{};
  tess::PathRequestRuntime runtime{};
  tess::JointMoveScratch joint_scratch{};
  std::vector<std::uint8_t> terrain_shadow{};
  std::vector<std::int16_t> current_xy{};
  std::vector<std::int16_t> previous_xy{};
  TrafficScenario selected_scenario;
  double accumulator_seconds = 0.0;
  double alpha = 0.0;
  double last_planning_us = 0.0;
  std::size_t last_planning_queries = 0;
  std::size_t last_guided_queries = 0;
  int last_fixed_ticks = 0;
  std::size_t last_advanced = 0;
  std::size_t last_waits = 0;
  std::size_t blocked = traffic_agents;
  std::size_t arrived = 0;
  std::size_t one_progress = 0;
  std::size_t longest_one_progress = 0;
  std::size_t maximum_planning_queries = 0;
#if TESS_DIAGNOSTICS_ENABLED
  tess::diagnostics::PathCounters last_path_counters{};
#endif

  void initialize_world() {
    for (auto& page : world.chunks()) {
      auto passable = page.field_span<PassableTag>();
      auto construction = page.field_span<ConstructionTag>();
      auto cost = page.field_span<CostTag>();
      auto occupancy = page.field_span<OccupancyTag>();
      auto reservation = page.field_span<ReservationTag>();
      for (std::size_t i = 0; i < passable.size(); ++i) {
        passable[i] = true;
        construction[i] = false;
        cost[i] = 1;
        occupancy[i] = false;
        reservation[i] = false;
      }
    }
    terrain_shadow.assign(
        static_cast<std::size_t>(traffic_width) * traffic_height, 0);
  }

  void initialize_scenario() {
    if (selected_scenario != TrafficScenario::Funnel &&
        selected_scenario != TrafficScenario::MultiGate) {
      return;
    }
    for (auto x = kBarrierMinX; x <= kBarrierMaxX; ++x) {
      for (auto y = 0; y < traffic_height; ++y) {
        if (scenario_has_opening(selected_scenario, y)) {
          continue;
        }
        const auto coord = tess::Coord3{x, y, 0};
        world.field<PassableTag>(coord) = false;
        world.field<ConstructionTag>(coord) = true;
        terrain_shadow[static_cast<std::size_t>(y) * traffic_width +
                       static_cast<std::size_t>(x)] = 1;
      }
    }
  }

  void initialize_agents() {
    const auto count = static_cast<std::size_t>(traffic_agents);
    agents.resize(count);
    current_xy.resize(count * 2);
    previous_xy.resize(count * 2);
    joint_scratch.reserve(count);
    replan_queue.reserve(count);
    replan_scratch.reserve_nodes(static_cast<std::size_t>(traffic_width) *
                                 traffic_height);
    replan_product.reserve_waypoints(3);
    replan_product.reserve_path_nodes(static_cast<std::size_t>(traffic_width) +
                                      traffic_height);
    replan_product.reserve_dependencies(world.chunks().size());
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agents[i].position = start_tile(selected_scenario, i);
      world.field<OccupancyTag>(agents[i].position) = true;
      tess::set_path_agent_goal(tick_state, agents[i],
                                goal_tile(selected_scenario, i));
      agents[i].phase = tess::PathAgentPhase::Blocked;
    }
    snapshot_current();
    previous_xy = current_xy;
    replan_queue.request_all(agents);
    tick_state.pathing_dirty = false;
  }

  void snapshot_current() {
    for (std::size_t i = 0; i < agents.size(); ++i) {
      current_xy[i * 2] = static_cast<std::int16_t>(agents[i].position.x);
      current_xy[i * 2 + 1] = static_cast<std::int16_t>(agents[i].position.y);
    }
  }

  void update_counts() {
    blocked = 0;
    arrived = 0;
    for (const auto& agent : agents) {
      if (!agent.has_goal) {
        ++arrived;
      } else if (agent.phase == tess::PathAgentPhase::Blocked) {
        ++blocked;
      }
    }
  }

  void fixed_tick() {
    previous_xy = current_xy;
    const auto planning_begin = std::chrono::steady_clock::now();
#if TESS_DIAGNOSTICS_ENABLED
    last_path_counters.reset();
    const auto path_counter_scope =
        tess::diagnostics::ScopedPathCounters{last_path_counters};
#endif
    last_guided_queries = 0;
    const auto planning = tess::process_path_agent_replans(
        agents, tick_state.routes, replan_queue, kSearchBudget,
        [&](std::size_t, tess::PathRequest request) {
          if (uses_guided_routes(selected_scenario)) {
            ++last_guided_queries;
            return guided_route(world, selected_scenario, request,
                                replan_scratch, replan_product);
          }
          return tess::weighted_astar_path<World, Driver>(world, request,
                                                          replan_scratch);
        });
    last_planning_us = std::chrono::duration<double, std::micro>(
                           std::chrono::steady_clock::now() - planning_begin)
                           .count();
    last_planning_queries = planning.submitted;
    maximum_planning_queries =
        std::max(maximum_planning_queries, planning.submitted);

    auto options = tess::PathAgentTickOptions{};
    options.max_blocked_retries = 0;
    options.blocked_exhaustion_policy =
        tess::BlockedAgentExhaustionPolicy::RemainBlocked;
    const auto stats = tess::tick_weighted_path_agents_with_joint_movement<
        World, Driver, 1, OccupancyTag, ReservationTag>(
        tick_state, world, agents, runtime, joint_scratch, options,
        tess::JointMoveOptions{tess::SwapPolicy::Permit}, 0, nullptr);
    last_advanced = stats.movement.advanced;
    last_waits = stats.movement.blocked_waits;
    if (last_advanced == 1) {
      ++one_progress;
      longest_one_progress = std::max(longest_one_progress, one_progress);
    } else {
      one_progress = 0;
    }
    snapshot_current();
    update_counts();
  }

  auto validate_passability() const -> bool {
    for (auto y = 0; y < traffic_height; ++y) {
      for (auto x = 0; x < traffic_width; ++x) {
        const auto coord = tess::Coord3{x, y, 0};
        const auto passable = world.field<PassableTag>(coord);
        const auto constructed = world.field<ConstructionTag>(coord);
        const auto positive_cost = world.field<CostTag>(coord) != 0;
        const auto legacy_usable = passable && positive_cost;
        const auto driver_usable = passable && !constructed && positive_cost;
        if (legacy_usable != driver_usable) {
          return false;
        }
      }
    }
    return true;
  }

  auto validate_planner() const -> bool {
    if (!validate_passability()) {
      return false;
    }
    if (!uses_guided_routes(selected_scenario)) {
      return true;
    }

    tess::PathScratch exact_scratch;
    tess::PathScratch first_scratch;
    tess::PathScratch second_scratch;
    const auto tile_count =
        static_cast<std::size_t>(traffic_width) * traffic_height;
    exact_scratch.reserve_nodes(tile_count);
    first_scratch.reserve_nodes(tile_count);
    second_scratch.reserve_nodes(tile_count);
    tess::WeightedPortalRouteProduct first_product;
    tess::WeightedPortalRouteProduct second_product;
    for (auto* product : {&first_product, &second_product}) {
      product->reserve_waypoints(3);
      product->reserve_path_nodes(static_cast<std::size_t>(traffic_width) +
                                  traffic_height);
      product->reserve_dependencies(world.chunks().size());
    }

    for (auto index = std::size_t{0}; index < agents.size(); ++index) {
      const auto request = tess::PathRequest{
          start_tile(selected_scenario, index),
          goal_tile(selected_scenario, index),
      };
      const auto exact = tess::weighted_astar_path<World, Driver>(
          world, request, exact_scratch);
      const auto first = guided_route(world, selected_scenario, request,
                                      first_scratch, first_product);
      const auto first_path =
          std::vector<tess::Coord3>{first.path.begin(), first.path.end()};
      const auto second = guided_route(world, selected_scenario, request,
                                       second_scratch, second_product);
      if (first.status != exact.status || first.cost != exact.cost ||
          second.status != first.status || second.cost != first.cost ||
          first_path.size() != second.path.size() ||
          !std::equal(first_path.begin(), first_path.end(),
                      second.path.begin()) ||
          first.status != tess::PathStatus::Found || first_path.empty() ||
          first_path.front() != request.start ||
          first_path.back() != request.goal ||
          first.cost + 1U != first_path.size()) {
        return false;
      }
      auto crossed_selected_gate = false;
      const auto gate_y = nearest_gate_row(selected_scenario, request.start.y);
      for (auto path_index = std::size_t{0}; path_index < first_path.size();
           ++path_index) {
        const auto coord = first_path[path_index];
        if (!world.field<PassableTag>(coord) ||
            world.field<ConstructionTag>(coord) ||
            world.field<CostTag>(coord) == 0) {
          return false;
        }
        if (path_index != 0 &&
            tess::manhattan_distance(first_path[path_index - 1], coord) != 1) {
          return false;
        }
        crossed_selected_gate = crossed_selected_gate ||
                                (coord.x >= kBarrierMinX &&
                                 coord.x <= kBarrierMaxX && coord.y == gate_y);
      }
      if (!crossed_selected_gate) {
        return false;
      }
    }
    return true;
  }

  auto agent_state_hash() const noexcept -> std::uint64_t {
    auto hash = std::uint64_t{1469598103934665603ULL};
    for (const auto& agent : agents) {
      for (const auto value : {
               agent.position.x,
               agent.position.y,
               agent.position.z,
               static_cast<std::int64_t>(agent.phase),
               static_cast<std::int64_t>(agent.has_goal),
           }) {
        const auto raw = static_cast<std::uint64_t>(value);
        for (auto byte = 0; byte < 8; ++byte) {
          hash ^= (raw >> static_cast<unsigned>(byte * 8)) & 0xffU;
          hash *= 1099511628211ULL;
        }
      }
    }
    return hash;
  }

  auto tick(double dt_seconds) -> double {
    const auto begin = std::chrono::steady_clock::now();
    accumulator_seconds += std::clamp(dt_seconds, 0.0, 0.25);
    const auto available = static_cast<int>(
        std::floor((accumulator_seconds + 1e-12) / kFixedStepSeconds));
    const auto granted = std::min(available, kMaxCatchUpTicks);
    last_fixed_ticks = granted;
    for (auto i = 0; i < granted; ++i) {
      fixed_tick();
      accumulator_seconds -= kFixedStepSeconds;
    }
    if (available > kMaxCatchUpTicks) {
      accumulator_seconds = std::fmod(accumulator_seconds, kFixedStepSeconds);
    }
    alpha = accumulator_seconds / kFixedStepSeconds;
    const auto elapsed_us = std::chrono::duration<double, std::micro>(
                                std::chrono::steady_clock::now() - begin)
                                .count();
    return granted == 0 ? -1.0 : elapsed_us / static_cast<double>(granted);
  }
};

auto scenario_name(TrafficScenario scenario) noexcept -> const char* {
  switch (scenario) {
    case TrafficScenario::Aligned:
      return "aligned";
    case TrafficScenario::ShuffledCrossing:
      return "shuffled-crossing";
    case TrafficScenario::Funnel:
      return "funnel";
    case TrafficScenario::MultiGate:
      return "multi-gate";
  }
  return "aligned";
}

TrafficModel::TrafficModel(TrafficScenario scenario)
    : impl_(std::make_unique<Impl>(scenario)) {}

TrafficModel::~TrafficModel() = default;

void TrafficModel::reset(TrafficScenario scenario) {
  impl_ = std::make_unique<Impl>(scenario);
}

auto TrafficModel::tick(double dt_seconds) -> double {
  return impl_->tick(dt_seconds);
}

auto TrafficModel::scenario() const noexcept -> TrafficScenario {
  return impl_->selected_scenario;
}

auto TrafficModel::terrain() const noexcept -> const std::uint8_t* {
  return impl_->terrain_shadow.data();
}

auto TrafficModel::current_agents() const noexcept -> const std::int16_t* {
  return impl_->current_xy.data();
}

auto TrafficModel::previous_agents() const noexcept -> const std::int16_t* {
  return impl_->previous_xy.data();
}

auto TrafficModel::interpolation_alpha() const noexcept -> double {
  return impl_->alpha;
}

auto TrafficModel::planning_us() const noexcept -> double {
  return impl_->last_planning_us;
}

auto TrafficModel::planning_queries_last_tick() const noexcept -> int {
  return static_cast<int>(impl_->last_planning_queries);
}

auto TrafficModel::guided_queries_last_tick() const noexcept -> int {
  return static_cast<int>(impl_->last_guided_queries);
}

auto TrafficModel::planning_touched_nodes_last_tick() const noexcept
    -> std::uint64_t {
#if TESS_DIAGNOSTICS_ENABLED
  return impl_->last_path_counters.touched_nodes;
#else
  return 0;
#endif
}

auto TrafficModel::planning_heap_pops_last_tick() const noexcept
    -> std::uint64_t {
#if TESS_DIAGNOSTICS_ENABLED
  return impl_->last_path_counters.heap_pops;
#else
  return 0;
#endif
}

auto TrafficModel::planning_neighbor_candidates_last_tick() const noexcept
    -> std::uint64_t {
#if TESS_DIAGNOSTICS_ENABLED
  return impl_->last_path_counters.neighbor_candidates;
#else
  return 0;
#endif
}

auto TrafficModel::planning_passability_checks_last_tick() const noexcept
    -> std::uint64_t {
#if TESS_DIAGNOSTICS_ENABLED
  return impl_->last_path_counters.passability_checks;
#else
  return 0;
#endif
}

auto TrafficModel::planning_reconstructed_nodes_last_tick() const noexcept
    -> std::uint64_t {
#if TESS_DIAGNOSTICS_ENABLED
  return impl_->last_path_counters.reconstructed_nodes;
#else
  return 0;
#endif
}

auto TrafficModel::fixed_ticks_last_call() const noexcept -> int {
  return impl_->last_fixed_ticks;
}

auto TrafficModel::planning_pending() const noexcept -> int {
  return static_cast<int>(impl_->replan_queue.pending());
}

auto TrafficModel::advanced_last_tick() const noexcept -> int {
  return static_cast<int>(impl_->last_advanced);
}

auto TrafficModel::movement_waits_last_tick() const noexcept -> int {
  return static_cast<int>(impl_->last_waits);
}

auto TrafficModel::blocked_agents() const noexcept -> int {
  return static_cast<int>(impl_->blocked);
}

auto TrafficModel::arrived_agents() const noexcept -> int {
  return static_cast<int>(impl_->arrived);
}

auto TrafficModel::one_progress_streak() const noexcept -> int {
  return static_cast<int>(impl_->one_progress);
}

auto TrafficModel::longest_one_progress_streak() const noexcept -> int {
  return static_cast<int>(impl_->longest_one_progress);
}

auto TrafficModel::max_planning_queries() const noexcept -> int {
  return static_cast<int>(impl_->maximum_planning_queries);
}

auto TrafficModel::agent_state_hash() const noexcept -> std::uint64_t {
  return impl_->agent_state_hash();
}

auto TrafficModel::validate_passability() const -> bool {
  return impl_->validate_passability();
}

auto TrafficModel::validate_planner() const -> bool {
  return impl_->validate_planner();
}

}  // namespace tess::examples::web_traffic

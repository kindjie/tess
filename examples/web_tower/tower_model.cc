#include "tower_model.h"

#include <tess/path/path_runtime.h>
#include <tess/sim/joint_movement.h>
#include <tess/sim/path_agent.h>
#include <tess/sim/path_agent_tick.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace tess::examples::web_tower {

namespace {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

constexpr int kWidth = width;
constexpr int kDepth = depth;
constexpr int kFloors = floors;
// Walkable floors sit on even z; the odd level between them is a solid
// slab. Without that slab the orthogonal lattice would connect z and
// z+1 everywhere and the tower would have no floors at all -- agents
// would drift vertically through open air.
constexpr int kLevels = kFloors * 2;
// Coordinates are 64-bit, so compute the top level in that width rather
// than multiplying in int and widening the result.
constexpr std::int64_t kTopLevel = std::int64_t{kFloors - 1} * 2;
constexpr std::uint32_t kMaxCost = 64;
// High enough that any open stairwell is preferred, low
// enough to stay under kMaxCost for a single step.
constexpr std::uint32_t kClosedStairCost = 32;

using Shape = tess::Shape<tess::Extent3{kWidth, kDepth, kLevels},
                          tess::Extent3{16, 16, 2}>;
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

using Traveler =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;

// Four stairwells, one per quadrant, each a 2x2 column running the full
// height. Closing one is the demo's single interaction: every agent
// whose route used it must find another.
struct Stairwell {
  int x;
  int y;
};
// Deliberately clear of both endpoint blocks. An agent's goal inside a
// stairwell would park it there for the whole leg, sealing the only
// route between floors for everyone behind it.
constexpr Stairwell kStairwells[] = {
    {20, 5}, {5, 20}, {kWidth - 7, 20}, {20, kDepth - 7}};
constexpr int kStairwellCount =
    static_cast<int>(sizeof(kStairwells) / sizeof(kStairwells[0]));

[[nodiscard]] auto in_stairwell(int index, int x, int y) -> bool {
  const auto& s = kStairwells[index];
  return x >= s.x && x < s.x + 3 && y >= s.y && y < s.y + 3;
}

// Whether `stair` rises out of the walkable floor `floor`. Two of the
// four serve each floor, and the pair alternates between the diagonal
// couples {0, 2} and {1, 3}, which buys both properties the demo needs.
//
// Crossing is compulsory: an agent arriving on a floor lands at one of
// this floor's risers, and every way up belongs to the other pair,
// elsewhere on the plate. Opening every stairwell through every slab
// instead makes the tower a lift shaft and leaves the rooms decorative
// -- measured that way as zero agent-ticks anywhere on an intermediate
// floor.
//
// A choice still exists: two risers serve each floor, so closing one
// diverts traffic to the other rather than changing nothing, which is
// what a single riser per floor would do.
[[nodiscard]] auto stair_serves_floor(int stair, int floor) -> bool {
  return (stair % 2) == (floor % 2);
}

[[nodiscard]] auto any_stairwell(int x, int y) -> int {
  for (int i = 0; i < kStairwellCount; ++i) {
    if (in_stairwell(i, x, y)) {
      return i;
    }
  }
  return -1;
}

// The clear area around each floor's endpoints, with a one-tile margin
// so the approach lanes are never partitioned either.
[[nodiscard]] auto in_endpoint_block(int x, int y, bool ground) -> bool {
  return ground ? (x >= 1 && x <= 19 && y >= 1 && y <= 15)
                : (x >= 28 && x <= 47 && y >= 32 && y <= 46);
}

// One floor: an outer wall, and interior partitions on a 16-tile grid
// pierced by doorways three tiles wide. Wide corridors keep the demo
// about routing between floors rather than about single-file queueing,
// which the two-dimensional lab already covers.
[[nodiscard]] auto floor_passable(int x, int y, int z) -> bool {
  if (x <= 0 || y <= 0 || x >= kWidth - 1 || y >= kDepth - 1) {
    return false;
  }
  // The endpoint floors carry the same room structure as the rest,
  // except inside the block that holds the endpoints themselves. Agents
  // park on their endpoint for the remainder of a leg, and a parked
  // agent standing in a doorway would seal the floor behind it, which
  // is what wedged earlier versions of this demo.
  if (z == 0 && in_endpoint_block(x, y, /*ground=*/true)) {
    return true;
  }
  if (z == kTopLevel && in_endpoint_block(x, y, /*ground=*/false)) {
    return true;
  }
  const bool partition = (x % 16 == 0) || (y % 16 == 0);
  if (!partition) {
    return true;
  }
  const bool doorway = (x % 16 == 0 && (y % 16 >= 6 && y % 16 <= 8)) ||
                       (y % 16 == 0 && (x % 16 >= 6 && x % 16 <= 8));
  return doorway;
}

}  // namespace

struct TowerModel::Impl {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathRequestRuntime runtime;
  tess::PathAgentTickState tick_state;
  tess::PathAgentReplanQueue replan_queue;
  tess::PathScratch replan_scratch;
  tess::JointMoveScratch joint_scratch;
  tess::SimClock sim_clock;
  tess::FixedStepAccumulator accumulator{20, 8};

  std::vector<std::uint8_t> tile_view;
  std::vector<std::int16_t> agent_xyz;
  std::vector<std::int16_t> previous_agent_xyz;
  double interpolation_alpha = 0.0;
  bool stair_open[kStairwellCount] = {true, true, true, true};
  bool outbound = true;
  int leg = 1;
  int completed_legs = 0;
  int stalled_ticks = 0;
  int advanced_last_tick = 0;
  int recovery_countdown = 0;
  std::vector<std::uint8_t> sealed;  // per agent: its tile is now solid.

  explicit Impl(int agent_count) {
    const auto count =
        static_cast<std::size_t>(std::clamp(agent_count, 1, max_agents));
    agents.resize(count);
    agent_xyz.assign(count * 3, -1);
    sealed.assign(count, 0);
    previous_agent_xyz.assign(count * 3, -1);
    tile_view.assign(static_cast<std::size_t>(kWidth) * kDepth * kLevels, 0);
    build_world();
    place_agents();
    publish_tiles();
    publish_positions();
    remember_positions();
  }

  void build_world() {
    world.template fill_field<PassableTag>(false);
    world.template fill_field<CostTag>(1U);
    world.template fill_field<OccupancyTag>(false);
    world.template fill_field<ReservationTag>(false);
    for (int z = 0; z < kLevels; ++z) {
      const bool walkable = (z % 2) == 0;
      for (int y = 0; y < kDepth; ++y) {
        for (int x = 0; x < kWidth; ++x) {
          const auto stair = any_stairwell(x, y);
          bool passable = false;
          if (walkable) {
            passable = floor_passable(x, y, z) || stair >= 0;
          } else {
            // The slab between floors is solid apart from the ONE
            // stairwell that serves this pair of floors. Opening every
            // stairwell through every slab would let an agent ride a
            // single column the whole height of the tower.
            passable = stair >= 0 && stair_serves_floor(stair, (z - 1) / 2);
          }
          const tess::Coord3 c{x, y, z};
          world.template field<PassableTag>(c) = passable;
          // A closed stairwell stays walkable but becomes expensive.
          // Making it impassable would strand whoever is inside it at
          // that instant -- every neighbour in the column seals at the
          // same moment -- and would force the interaction to be
          // refused while anyone is on the stairs, which is most of the
          // time. Pricing it instead diverts new routes, lets the
          // occupants walk out, and cannot make a floor unreachable.
          world.template field<CostTag>(c) =
              (stair >= 0 && !stair_open[stair]) ? kClosedStairCost : 1U;
        }
      }
    }
    for (std::uint64_t k = 0; k < tess::ShapeTraits<Shape>::chunk_count; ++k) {
      world.mark_content_changed(tess::ChunkKey{k});
    }
  }

  // Endpoints occupy a 14x14 block inside one room, which exceeds
  // max_agents: an arrived agent keeps its tile, so two agents sharing
  // a goal would deadlock the second one permanently.
  // Endpoints occupy alternate ROWS inside one room, with the rows
  // between them left clear as approach corridors.
  //
  // Two weaker layouts fail here, both worth recording. A solid block
  // fills from the outside in and seals the interior goals behind
  // arrived agents. A checkerboard looks like it fixes that and is
  // worse: on a 4-connected lattice the unoccupied tiles of a
  // checkerboard share no orthogonal edge, so once the pattern fills,
  // every free tile is enclosed by occupied ones and nothing can move
  // at all. Striped rows keep each free row connected end to end.
  static constexpr int kEndpointSpan = 17;
  static constexpr int kEndpointAisle = 8;  // local column left clear
  static constexpr int kEndpointRows = 7;   // rows 0, 2, ... 12
  static constexpr int kEndpointCount = (kEndpointSpan - 1) * kEndpointRows;
  static_assert(kEndpointCount >= max_agents,
                "every agent needs its own reachable endpoint tile");

  // Alternate rows hold endpoints and the rows between them are
  // corridors -- but a full row of parked agents is itself a wall, so
  // the corridors would be isolated from each other. One column is
  // therefore kept permanently clear as an access aisle crossing every
  // row. (A checkerboard instead of rows fails harder: on a 4-connected
  // lattice its free tiles share no orthogonal edge at all.)
  // Rewrites one stairwell's entry costs across every level, leaving
  // passability, occupancy, and sealed tiles untouched.
  void reprice_stairwell(int index) {
    const auto& s = kStairwells[index];
    const std::uint32_t cost = stair_open[index] ? 1U : kClosedStairCost;
    for (int z = 0; z < kLevels; ++z) {
      for (int y = s.y; y < s.y + 3; ++y) {
        for (int x = s.x; x < s.x + 3; ++x) {
          const tess::Coord3 c{x, y, z};
          world.template field<CostTag>(c) = cost;
          world.mark_content_changed(
              tess::chunk_key<Shape>(tess::chunk_coord<Shape>(c)));
        }
      }
    }
  }

  [[nodiscard]] static auto endpoint_offset(std::size_t i)
      -> std::pair<int, int> {
    const auto lane = static_cast<int>(i) % kEndpointCount;
    const auto column = lane % (kEndpointSpan - 1);
    return {column < kEndpointAisle ? column : column + 1,
            2 * (lane / (kEndpointSpan - 1))};
  }

  [[nodiscard]] static auto start_tile(std::size_t i) -> tess::Coord3 {
    const auto [dx, dy] = endpoint_offset(i);
    return tess::Coord3{2 + dx, 2 + dy, 0};
  }

  [[nodiscard]] static auto goal_tile(std::size_t i) -> tess::Coord3 {
    const auto [dx, dy] = endpoint_offset(i);
    return tess::Coord3{29 + dx, 33 + dy, kTopLevel};
  }

  void place_agents() {
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agents[i] = tess::PathAgentState{};
      agents[i].position = outbound ? start_tile(i) : goal_tile(i);
      const auto target = outbound ? goal_tile(i) : start_tile(i);
      tess::set_path_agent_goal(tick_state, agents[i], target);
    }
    tess::mark_pathing_dirty(tick_state);
  }

  void publish_tiles() {
    for (int z = 0; z < kLevels; ++z) {
      for (int y = 0; y < kDepth; ++y) {
        for (int x = 0; x < kWidth; ++x) {
          const auto index =
              (static_cast<std::size_t>(z) * static_cast<std::size_t>(kDepth) +
               static_cast<std::size_t>(y)) *
                  static_cast<std::size_t>(kWidth) +
              static_cast<std::size_t>(x);
          tile_view[index] =
              world.template field<PassableTag>(tess::Coord3{x, y, z}) ? 1U
                                                                       : 0U;
        }
      }
    }
  }

  void remember_positions() { previous_agent_xyz = agent_xyz; }

  void publish_positions() {
    for (std::size_t i = 0; i < agents.size(); ++i) {
      agent_xyz[i * 3 + 0] = static_cast<std::int16_t>(agents[i].position.x);
      agent_xyz[i * 3 + 1] = static_cast<std::int16_t>(agents[i].position.y);
      agent_xyz[i * 3 + 2] = static_cast<std::int16_t>(agents[i].position.z);
    }
  }

  [[nodiscard]] auto set_stairwell(int index, bool open) -> bool {
    if (index < 0 || index >= kStairwellCount) {
      return false;
    }
    if (stair_open[index] == open) {
      return true;
    }

    stair_open[index] = open;
    // Only the stairwell's costs change. Rebuilding the world would
    // refill every field, which clears live OccupancyTag and reopens
    // the tiles that arrived agents have sealed -- joint movement
    // trusts both during admission, so two agents could then be
    // admitted onto one tile.
    reprice_stairwell(index);
    publish_tiles();
    // A sealed stairwell can invalidate any retained route, so this is
    // the one edit that legitimately replans everything.
    tess::mark_pathing_dirty(tick_state);
    return true;
  }

  // Agents standing in a stairwell right now. Counting every route that
  // still changes height would report almost the whole fleet, since any
  // ground-to-top route crosses a floor eventually.
  [[nodiscard]] auto climbing() const -> int {
    int count = 0;
    for (const auto& agent : agents) {
      if (any_stairwell(static_cast<int>(agent.position.x),
                        static_cast<int>(agent.position.y)) >= 0) {
        ++count;
      }
    }
    return count;
  }

  static constexpr int kRecoveryPeriodTicks = 16;

  // An arrived agent never moves again, but the planner is deliberately
  // occupancy-blind: it would keep returning the shortest route straight
  // through a parked agent, and the follower would wait forever on a
  // step that can never clear. Sealing the tile is what makes the
  // planner route around a finished agent -- the same reason the
  // two-dimensional demo marks settled tiles.
  void seal_arrived_tiles() {
    bool sealed_any = false;
    for (std::size_t i = 0; i < agents.size(); ++i) {
      if (sealed[i] != 0 || agents[i].has_goal) {
        continue;
      }
      world.template field<PassableTag>(agents[i].position) = false;
      world.mark_content_changed(
          tess::chunk_key<Shape>(tess::chunk_coord<Shape>(agents[i].position)));
      sealed[i] = 1;
      sealed_any = true;
    }
    if (sealed_any) {
      publish_tiles();
      tess::mark_pathing_dirty(tick_state);
    }
  }

  [[nodiscard]] auto blocked_count() const -> int {
    int count = 0;
    for (const auto& agent : agents) {
      if (agent.has_goal && agent.phase == tess::PathAgentPhase::Blocked) {
        ++count;
      }
    }
    return count;
  }

  [[nodiscard]] auto all_settled() const -> bool {
    return settled_count() == static_cast<int>(agents.size());
  }

  [[nodiscard]] auto settled_count() const -> int {
    int settled = 0;
    for (const auto& agent : agents) {
      if (!agent.has_goal || agent.phase == tess::PathAgentPhase::Unreachable) {
        ++settled;
      }
    }
    return settled;
  }

  auto tick(double dt_seconds) -> double {
    const auto frame = accumulator.consume(dt_seconds, tess::SimTimeControl{});
    for (std::size_t step = 0; step < frame.ticks; ++step) {
      // Record where agents were BEFORE this tick, then publish where
      // they are after it. Snapshotting only before movement would
      // leave `current_agents()` a whole tick behind the counters.
      remember_positions();
      auto options = tess::PathAgentTickOptions{};
      // Retry an occupancy-blocked step rather than sleeping on it:
      // this tower's only routes between floors are stairwells, so a
      // transient shuffle at a landing is the normal case.
      options.max_blocked_retries = 32;
      options.blocked_exhaustion_policy =
          tess::BlockedAgentExhaustionPolicy::RemainBlocked;
      const auto stats = tess::tick_weighted_path_agents_with_joint_movement<
          World, Traveler, kMaxCost, OccupancyTag, ReservationTag>(
          tick_state, world, agents, runtime, joint_scratch, options,
          tess::JointMoveOptions{tess::SwapPolicy::Permit}, 0, nullptr);
      advanced_last_tick = static_cast<int>(stats.movement.advanced);
      stalled_ticks = stats.movement.advanced == 0 ? stalled_ticks + 1 : 0;
      sim_clock.tick += 1;
      // Blocked agents sleep once their retry budget is spent, and this
      // demo does not carry the colony's recovery scheduler. Waking
      // them on a GLOBAL stall is not enough: while any other agent
      // still moves, a wedged one would never be revisited. Sweep on a
      // fixed period instead, whenever anyone is blocked.
      publish_positions();
      seal_arrived_tiles();
      ++recovery_countdown;
      if (recovery_countdown >= kRecoveryPeriodTicks) {
        recovery_countdown = 0;
        if (blocked_count() > 0) {
          tess::mark_pathing_dirty(tick_state);
        }
      }
    }
    interpolation_alpha = frame.alpha;
    return interpolation_alpha;
  }

  auto relaunch() -> int {
    outbound = !outbound;
    if (outbound) {
      ++completed_legs;
    }
    ++leg;
    std::fill(sealed.begin(), sealed.end(), std::uint8_t{0});
    build_world();
    publish_tiles();
    place_agents();
    return leg;
  }
};

TowerModel::TowerModel(int agent_count)
    : impl_(std::make_unique<Impl>(agent_count)) {}
TowerModel::~TowerModel() = default;

auto TowerModel::set_stairwell(int index, bool open) -> bool {
  return impl_->set_stairwell(index, open);
}
auto TowerModel::stairwell_count() const noexcept -> int {
  return kStairwellCount;
}
auto TowerModel::stairwell_open(int index) const noexcept -> bool {
  return index >= 0 && index < kStairwellCount && impl_->stair_open[index];
}
auto TowerModel::stairwell_x(int index) const noexcept -> int {
  return index >= 0 && index < kStairwellCount ? kStairwells[index].x : 0;
}
auto TowerModel::stairwell_y(int index) const noexcept -> int {
  return index >= 0 && index < kStairwellCount ? kStairwells[index].y : 0;
}
auto TowerModel::stairwell_floor(int) const noexcept -> int { return 0; }

auto TowerModel::tick(double dt_seconds) -> double {
  return impl_->tick(dt_seconds);
}
auto TowerModel::relaunch() -> int { return impl_->relaunch(); }

auto TowerModel::agent_count() const noexcept -> int {
  return static_cast<int>(impl_->agents.size());
}
auto TowerModel::arrived() const -> int {
  int count = 0;
  for (const auto& agent : impl_->agents) {
    if (!agent.has_goal) {
      ++count;
    }
  }
  return count;
}
auto TowerModel::unreachable() const -> int {
  int count = 0;
  for (const auto& agent : impl_->agents) {
    if (agent.phase == tess::PathAgentPhase::Unreachable) {
      ++count;
    }
  }
  return count;
}
auto TowerModel::crowd_blocked() const -> int {
  int count = 0;
  for (const auto& agent : impl_->agents) {
    if (agent.has_goal && agent.phase == tess::PathAgentPhase::Blocked) {
      ++count;
    }
  }
  return count;
}
auto TowerModel::turnaround_ready() const -> bool {
  return impl_->settled_count() == static_cast<int>(impl_->agents.size());
}
auto TowerModel::leg() const noexcept -> int { return impl_->leg; }
auto TowerModel::completed_legs() const noexcept -> int {
  return impl_->completed_legs;
}
auto TowerModel::planning_pending() const noexcept -> int {
  return static_cast<int>(impl_->replan_queue.pending());
}
auto TowerModel::advanced_last_tick() const noexcept -> int {
  return impl_->advanced_last_tick;
}
auto TowerModel::stalled_ticks() const noexcept -> int {
  return impl_->stalled_ticks;
}
auto TowerModel::climbing() const noexcept -> int { return impl_->climbing(); }

auto TowerModel::agent_debug(int i, int* px, int* py, int* pz, int* gx, int* gy,
                             int* gz, int* phase) const -> bool {
  if (i < 0 || static_cast<std::size_t>(i) >= impl_->agents.size()) {
    return false;
  }
  const auto& a = impl_->agents[static_cast<std::size_t>(i)];
  *px = static_cast<int>(a.position.x);
  *py = static_cast<int>(a.position.y);
  *pz = static_cast<int>(a.position.z);
  *gx = static_cast<int>(a.goal.x);
  *gy = static_cast<int>(a.goal.y);
  *gz = static_cast<int>(a.goal.z);
  *phase = static_cast<int>(a.phase);
  return true;
}

auto TowerModel::tiles() const noexcept -> const std::uint8_t* {
  return impl_->tile_view.data();
}
auto TowerModel::current_agents() const noexcept -> const std::int16_t* {
  return impl_->agent_xyz.data();
}
auto TowerModel::previous_agents() const noexcept -> const std::int16_t* {
  return impl_->previous_agent_xyz.data();
}
auto TowerModel::interpolation_alpha() const noexcept -> double {
  return impl_->interpolation_alpha;
}

}  // namespace tess::examples::web_tower

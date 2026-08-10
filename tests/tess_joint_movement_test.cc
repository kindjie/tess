#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

using Joint2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>>;
using World = tess::AlwaysResidentWorld<Joint2D, Schema>;
// The weighted planner requires a MovementClass; raw tags are only accepted
// by the movement layer itself.
using Walker =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;

void fill_world(World& world) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    auto occupancy = page.template field_span<OccupancyTag>();
    auto reservation = page.template field_span<ReservationTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
      cost[i] = 1u;
      occupancy[i] = false;
      reservation[i] = false;
    }
  }
}

// Builds an agent standing at `route.front()` following `route` toward
// `route.back()`, with occupancy claimed, exactly as the tick drivers leave a
// Found agent between planning and movement.
auto add_agent(World& world, std::vector<tess::PathAgentState>& agents,
               tess::PathAgentRoutes& routes, std::vector<tess::Coord3> route)
    -> std::size_t {
  tess::PathAgentState agent;
  agent.position = route.front();
  agent.goal = route.back();
  agent.has_goal = true;
  agent.status = tess::PathStatus::Found;
  agent.phase = tess::PathAgentPhase::Following;
  world.field<OccupancyTag>(agent.position) = true;
  agents.push_back(agent);
  routes.routes.push_back(std::move(route));
  return agents.size() - 1;
}

template <typename... Args>
auto joint(World& world, std::vector<tess::PathAgentState>& agents,
           const tess::PathAgentRoutes& routes, tess::JointMoveScratch& scratch,
           Args&&... args) -> tess::JointMoveStats {
  return tess::advance_path_agents_with_joint_movement<
      World, PassableTag, OccupancyTag, ReservationTag>(
      world, std::span<tess::PathAgentState>(agents), routes, scratch,
      std::forward<Args>(args)...);
}

auto legacy(World& world, std::vector<tess::PathAgentState>& agents,
            const tess::PathAgentRoutes& routes) -> tess::PathAgentFrameStats {
  return tess::advance_path_agents_with_movement<World, PassableTag,
                                                 OccupancyTag, ReservationTag>(
      world, std::span<tess::PathAgentState>(agents), routes, 1, 0);
}

TEST(TessJointMovement, ChainDrainsInOneTickWherePerAgentCommitCannot) {
  // Rear-to-front span order: the per-agent commit validates the rear agent
  // while the tile ahead is still occupied, so only the front agent moves.
  const auto build = [](World& world, std::vector<tess::PathAgentState>& agents,
                        tess::PathAgentRoutes& routes) {
    fill_world(world);
    add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
    add_agent(world, agents, routes, {{2, 1, 0}, {3, 1, 0}, {4, 1, 0}});
    add_agent(world, agents, routes, {{3, 1, 0}, {4, 1, 0}, {5, 1, 0}});
  };

  {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    const auto stats = legacy(world, agents, routes);
    EXPECT_EQ(stats.advanced, 1u);  // the documented per-agent gap
  }

  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  build(world, agents, routes);
  tess::JointMoveScratch scratch;
  const auto stats = joint(world, agents, routes, scratch);
  EXPECT_EQ(stats.frame.advanced, 3u);
  EXPECT_EQ(stats.chained, 2u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{2, 1, 0}));
  EXPECT_EQ(agents[1].position, (tess::Coord3{3, 1, 0}));
  EXPECT_EQ(agents[2].position, (tess::Coord3{4, 1, 0}));
  EXPECT_FALSE(world.field<OccupancyTag>(tess::Coord3{1, 1, 0}));
  EXPECT_TRUE(world.field<OccupancyTag>(tess::Coord3{4, 1, 0}));
}

TEST(TessJointMovement, FourCycleRotatesUnderForbid) {
  // The minimal wants-cycle on a 4-connected grid has length four (grid girth
  // is four); it involves no shared edge, so even Forbid admits it.
  const auto build = [](World& world, std::vector<tess::PathAgentState>& agents,
                        tess::PathAgentRoutes& routes) {
    fill_world(world);
    add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});
    add_agent(world, agents, routes, {{2, 1, 0}, {2, 2, 0}});
    add_agent(world, agents, routes, {{2, 2, 0}, {1, 2, 0}});
    add_agent(world, agents, routes, {{1, 2, 0}, {1, 1, 0}});
  };

  {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    const auto stats = legacy(world, agents, routes);
    EXPECT_EQ(stats.advanced, 0u);  // the documented per-agent gap
  }

  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  build(world, agents, routes);
  tess::JointMoveScratch scratch;
  const auto stats = joint(world, agents, routes, scratch);
  EXPECT_EQ(stats.frame.advanced, 4u);
  EXPECT_EQ(stats.rotations, 1u);
  EXPECT_EQ(stats.swaps, 0u);
  EXPECT_EQ(stats.frame.arrived, 4u);  // each goal was the next tile over
  for (const auto coord : {tess::Coord3{1, 1, 0}, tess::Coord3{2, 1, 0},
                           tess::Coord3{2, 2, 0}, tess::Coord3{1, 2, 0}}) {
    EXPECT_TRUE(world.field<OccupancyTag>(coord));
  }
}

TEST(TessJointMovement, HeadOnPairFollowsSwapPolicy) {
  const auto build = [](World& world, std::vector<tess::PathAgentState>& agents,
                        tess::PathAgentRoutes& routes) {
    fill_world(world);
    add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});
    add_agent(world, agents, routes, {{2, 1, 0}, {1, 1, 0}});
  };

  {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    const auto stats = legacy(world, agents, routes);
    EXPECT_EQ(stats.advanced, 0u);  // the documented per-agent gap
  }

  {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    tess::JointMoveScratch scratch;
    const auto stats = joint(world, agents, routes, scratch);  // Forbid
    EXPECT_EQ(stats.frame.advanced, 0u);
    EXPECT_EQ(stats.swaps_denied, 1u);
    EXPECT_EQ(stats.frame.movement_failures.occupied, 2u);
    // Occupancy failures retain the Found route for a same-step retry.
    EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
    EXPECT_EQ(agents[0].status, tess::PathStatus::Found);
  }

  {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    tess::JointMoveScratch scratch;
    const auto stats = joint(world, agents, routes, scratch,
                             tess::JointMoveOptions{tess::SwapPolicy::Permit});
    EXPECT_EQ(stats.frame.advanced, 2u);
    EXPECT_EQ(stats.swaps, 1u);
    EXPECT_EQ(stats.frame.arrived, 2u);
    EXPECT_EQ(agents[0].position, (tess::Coord3{2, 1, 0}));
    EXPECT_EQ(agents[1].position, (tess::Coord3{1, 1, 0}));
    EXPECT_TRUE(world.field<OccupancyTag>(tess::Coord3{1, 1, 0}));
    EXPECT_TRUE(world.field<OccupancyTag>(tess::Coord3{2, 1, 0}));
  }
}

TEST(TessJointMovement, CyclesFreeNoTilesSoFollowersWait) {
  // A cycle is volume-preserving: it refills every tile it vacates, so a
  // follower whose next tile belongs to a rotating cycle cannot enter it
  // this tick -- the predecessor in the cycle does. The follower waits as an
  // ordinary occupancy block and proceeds next tick.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});
  add_agent(world, agents, routes, {{2, 1, 0}, {2, 2, 0}});
  add_agent(world, agents, routes, {{2, 2, 0}, {1, 2, 0}});
  add_agent(world, agents, routes, {{1, 2, 0}, {1, 1, 0}});
  add_agent(world, agents, routes, {{3, 1, 0}, {2, 1, 0}, {2, 0, 0}});

  tess::JointMoveScratch scratch;
  const auto stats = joint(world, agents, routes, scratch);  // Forbid
  EXPECT_EQ(stats.rotations, 1u);
  EXPECT_EQ(stats.chained, 0u);
  EXPECT_EQ(stats.frame.advanced, 4u);
  EXPECT_EQ(agents[4].position, (tess::Coord3{3, 1, 0}));
  EXPECT_EQ(agents[4].phase, tess::PathAgentPhase::Blocked);
  EXPECT_EQ(agents[4].status, tess::PathStatus::Found);
  // The rotation left (2, 1) occupied by the cycle's own predecessor.
  EXPECT_TRUE(world.field<OccupancyTag>(tess::Coord3{2, 1, 0}));
}

TEST(TessJointMovement, SwapsFreeNoTilesSoFollowersWait) {
  // Same invariant for the two-agent cycle: an exchange refills both tiles,
  // so a follower behind either half waits.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes, {{2, 1, 0}, {3, 1, 0}});
  add_agent(world, agents, routes, {{3, 1, 0}, {2, 1, 0}});
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}, {2, 2, 0}});

  tess::JointMoveScratch scratch;
  const auto stats = joint(world, agents, routes, scratch,
                           tess::JointMoveOptions{tess::SwapPolicy::Permit});
  EXPECT_EQ(stats.swaps, 1u);
  EXPECT_EQ(stats.chained, 0u);
  EXPECT_EQ(stats.frame.advanced, 2u);
  EXPECT_EQ(agents[2].position, (tess::Coord3{1, 1, 0}));
  EXPECT_EQ(agents[2].phase, tess::PathAgentPhase::Blocked);
}

TEST(TessJointMovement, ObserverSeesTheFullyAppliedConfiguration) {
  // Callbacks fire after the whole round applies, so each one observes the
  // final positions of BOTH halves of a swap, and an injective mirror can
  // buffer the round safely.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});
  add_agent(world, agents, routes, {{2, 1, 0}, {1, 1, 0}});

  tess::JointMoveScratch scratch;
  std::size_t callbacks = 0;
  bool consistent = true;
  const auto stats = tess::advance_path_agents_with_joint_movement<
      World, PassableTag, OccupancyTag, ReservationTag>(
      world, std::span<tess::PathAgentState>(agents), routes, scratch,
      tess::JointMoveOptions{tess::SwapPolicy::Permit}, 1u, 0u,
      [&](std::size_t index, tess::Coord3 from, tess::Coord3 to) {
        ++callbacks;
        consistent = consistent && agents[index].position == to &&
                     !(from == to) &&
                     agents[0].position == (tess::Coord3{2, 1, 0}) &&
                     agents[1].position == (tess::Coord3{1, 1, 0});
      });
  EXPECT_EQ(stats.swaps, 1u);
  EXPECT_EQ(callbacks, 2u);
  EXPECT_TRUE(consistent);
}

TEST(TessJointMovement, PermitOnDeadlockWaitsForTheBlockedBudget) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});
  add_agent(world, agents, routes, {{2, 1, 0}, {1, 1, 0}});
  tess::JointMoveScratch scratch;
  const auto options =
      tess::JointMoveOptions{tess::SwapPolicy::PermitOnDeadlock, 4u};

  agents[0].blocked_retries = 3;
  agents[1].blocked_retries = 4;
  auto stats = joint(world, agents, routes, scratch, options);
  EXPECT_EQ(stats.frame.advanced, 0u);
  EXPECT_EQ(stats.swaps_denied, 1u);

  agents[0].blocked_retries = 4;
  agents[0].status = tess::PathStatus::Found;  // retained by the denial
  agents[1].status = tess::PathStatus::Found;
  stats = joint(world, agents, routes, scratch, options);
  EXPECT_EQ(stats.frame.advanced, 2u);
  EXPECT_EQ(stats.swaps, 1u);
}

TEST(TessJointMovement, ReservedDestinationBlocksEvenWhenVacated) {
  // Reservations are an application-owned do-not-enter flag. The per-agent
  // commit reports Occupied for a tile that is both occupied and reserved,
  // but joint admission must not let the reservation vanish behind a vacating
  // occupant, so the reserved verdict wins for pending moves.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
  add_agent(world, agents, routes, {{2, 1, 0}, {3, 1, 0}, {4, 1, 0}});
  world.field<ReservationTag>(tess::Coord3{2, 1, 0}) = true;

  tess::JointMoveScratch scratch;
  const auto stats = joint(world, agents, routes, scratch);
  // The front agent moves; the rear agent's destination is being vacated but
  // stays reserved, so it must not be entered.
  EXPECT_EQ(stats.frame.advanced, 1u);
  EXPECT_EQ(stats.frame.movement_failures.reserved, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 1, 0}));
  EXPECT_EQ(agents[1].position, (tess::Coord3{3, 1, 0}));
  EXPECT_TRUE(world.field<ReservationTag>(tess::Coord3{2, 1, 0}));
}

TEST(TessJointMovement, EntryClearsTheDestinationReservation) {
  // Matches commit_movement_intent: a successful entry defensively clears the
  // (necessarily false) reservation bit on the destination.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});
  tess::JointMoveScratch scratch;
  const auto stats = joint(world, agents, routes, scratch);
  EXPECT_EQ(stats.frame.advanced, 1u);
  EXPECT_FALSE(world.field<ReservationTag>(tess::Coord3{2, 1, 0}));
}

TEST(TessJointMovement, ImpassableNextTileInvalidatesTheRoute) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
  world.field<PassableTag>(tess::Coord3{2, 1, 0}) = false;

  tess::JointMoveScratch scratch;
  const auto stats = joint(world, agents, routes, scratch);
  EXPECT_EQ(stats.frame.advanced, 0u);
  EXPECT_EQ(stats.frame.movement_failures.blocked, 1u);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  // Route-invalidating failures drop Found so scoped resubmission replans.
  EXPECT_EQ(agents[0].status, tess::PathStatus::NoPath);
}

TEST(TessJointMovement, SharedFreeDestinationGoesToTheEarlierAgent) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes, {{1, 1, 0}, {1, 2, 0}});
  add_agent(world, agents, routes, {{1, 3, 0}, {1, 2, 0}});

  tess::JointMoveScratch scratch;
  const auto stats = joint(world, agents, routes, scratch);
  EXPECT_EQ(stats.frame.advanced, 1u);
  EXPECT_EQ(stats.frame.movement_failures.occupied, 1u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 2, 0}));
  EXPECT_EQ(agents[1].position, (tess::Coord3{1, 3, 0}));
}

TEST(TessJointMovement, DirtyMaskMarksBothChunksPerMove) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  // x=7 -> x=8 crosses the 8x8 chunk boundary.
  add_agent(world, agents, routes, {{7, 1, 0}, {8, 1, 0}});
  const auto from_key =
      tess::chunk_key<Joint2D>(tess::chunk_coord<Joint2D>({7, 1, 0}));
  const auto to_key =
      tess::chunk_key<Joint2D>(tess::chunk_coord<Joint2D>({8, 1, 0}));
  const auto from_version = world.meta(from_key).version;
  const auto to_version = world.meta(to_key).version;

  tess::JointMoveScratch scratch;
  const auto stats =
      joint(world, agents, routes, scratch, tess::JointMoveOptions{}, 1u, 1u);
  EXPECT_EQ(stats.frame.advanced, 1u);
  EXPECT_GT(world.meta(from_key).version, from_version);
  EXPECT_GT(world.meta(to_key).version, to_version);
}

TEST(TessJointMovement, MaxStepsZeroSpendsNoBudgetAndMovesNothing) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});
  tess::JointMoveScratch scratch;
  const auto stats =
      joint(world, agents, routes, scratch, tess::JointMoveOptions{}, 0u, 0u);
  EXPECT_EQ(stats.frame.advanced, 0u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 1, 0}));
}

TEST(TessJointMovement, MultiStepDrainsAChainFurther) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  add_agent(world, agents, routes,
            {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}, {4, 1, 0}});
  add_agent(world, agents, routes,
            {{2, 1, 0}, {3, 1, 0}, {4, 1, 0}, {5, 1, 0}});

  tess::JointMoveScratch scratch;
  const auto stats =
      joint(world, agents, routes, scratch, tess::JointMoveOptions{}, 2u, 0u);
  EXPECT_EQ(stats.frame.advanced, 4u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{3, 1, 0}));
  EXPECT_EQ(agents[1].position, (tess::Coord3{4, 1, 0}));
}

TEST(TessJointMovement, DeterministicAcrossIdenticalRuns) {
  const auto run = [](std::vector<tess::Coord3>& positions,
                      tess::JointMoveStats& stats) {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    fill_world(world);
    add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
    add_agent(world, agents, routes, {{2, 1, 0}, {3, 1, 0}, {4, 1, 0}});
    add_agent(world, agents, routes, {{5, 1, 0}, {4, 1, 0}});
    add_agent(world, agents, routes, {{1, 3, 0}, {2, 3, 0}});
    add_agent(world, agents, routes, {{2, 3, 0}, {1, 3, 0}});
    tess::JointMoveScratch scratch;
    stats = joint(world, agents, routes, scratch,
                  tess::JointMoveOptions{tess::SwapPolicy::Permit});
    positions.clear();
    for (const auto& agent : agents) {
      positions.push_back(agent.position);
    }
  };

  std::vector<tess::Coord3> first;
  std::vector<tess::Coord3> second;
  tess::JointMoveStats first_stats;
  tess::JointMoveStats second_stats;
  run(first, first_stats);
  run(second, second_stats);
  EXPECT_EQ(first, second);
  EXPECT_EQ(first_stats.frame.advanced, second_stats.frame.advanced);
  EXPECT_EQ(first_stats.swaps, second_stats.swaps);
  EXPECT_EQ(first_stats.chained, second_stats.chained);
}

TEST(TessJointMovement, WarmPassAllocatesNothingAfterReserve) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world);
  // A chain, so the pass exercises claims, the occupant index, and the
  // fixpoint; routes are long enough that agents stay active across calls.
  for (int i = 0; i < 8; ++i) {
    std::vector<tess::Coord3> route;
    for (int step = 0; step <= 16; ++step) {
      route.push_back({1 + i + step, 1, 0});
    }
    add_agent(world, agents, routes, std::move(route));
  }
  tess::JointMoveScratch scratch;
  scratch.reserve(agents.size());
  (void)joint(world, agents, routes, scratch);  // warm

  tess_test::ScopedAllocationCounter counter;
  const auto stats = joint(world, agents, routes, scratch);
  EXPECT_EQ(counter.count(), 0u);
  EXPECT_EQ(counter.bytes(), 0u);
  EXPECT_EQ(stats.frame.advanced, 8u);
}

// `reserve` is the whole documented surface of the scratch; the round
// buffers behind it are not. Asserting only that `reserve` still compiles
// would pass just as well with every buffer public again, so each buffer
// name gets its own probe, and `BufferProbeControl` carries all eleven names
// in public: without it, a mistyped member name would make a probe
// unsatisfiable for every type and the negative assertion could not fail.
struct BufferProbeControl {
  int occupant_key = 0;
  int occupant_agent = 0;
  int claimed = 0;
  int desired = 0;
  int state = 0;
  int failure = 0;
  int cycle_walk = 0;
  int on_walk = 0;
  int walked = 0;
  int committed = 0;
  int committed_from = 0;
};

template <typename T>
concept HasMemberOccupantKey = requires(T& value) { value.occupant_key; };
template <typename T>
concept HasMemberOccupantAgent = requires(T& value) { value.occupant_agent; };
template <typename T>
concept HasMemberClaimed = requires(T& value) { value.claimed; };
template <typename T>
concept HasMemberDesired = requires(T& value) { value.desired; };
template <typename T>
concept HasMemberState = requires(T& value) { value.state; };
template <typename T>
concept HasMemberFailure = requires(T& value) { value.failure; };
template <typename T>
concept HasMemberCycleWalk = requires(T& value) { value.cycle_walk; };
template <typename T>
concept HasMemberOnWalk = requires(T& value) { value.on_walk; };
template <typename T>
concept HasMemberWalked = requires(T& value) { value.walked; };
template <typename T>
concept HasMemberCommitted = requires(T& value) { value.committed; };
template <typename T>
concept HasMemberCommittedFrom = requires(T& value) { value.committed_from; };

TEST(TessJointMovement, ScratchRoundBuffersAreNotPartOfTheSurface) {
  using Scratch = tess::JointMoveScratch;
  using Control = BufferProbeControl;

  // Every probe is satisfiable, so none of the negative assertions below can
  // pass merely because a member name was mistyped.
  static_assert(HasMemberOccupantKey<Control>);
  static_assert(HasMemberOccupantAgent<Control>);
  static_assert(HasMemberClaimed<Control>);
  static_assert(HasMemberDesired<Control>);
  static_assert(HasMemberState<Control>);
  static_assert(HasMemberFailure<Control>);
  static_assert(HasMemberCycleWalk<Control>);
  static_assert(HasMemberOnWalk<Control>);
  static_assert(HasMemberWalked<Control>);
  static_assert(HasMemberCommitted<Control>);
  static_assert(HasMemberCommittedFrom<Control>);

  static_assert(!HasMemberOccupantKey<Scratch>);
  static_assert(!HasMemberOccupantAgent<Scratch>);
  static_assert(!HasMemberClaimed<Scratch>);
  static_assert(!HasMemberDesired<Scratch>);
  static_assert(!HasMemberState<Scratch>);
  static_assert(!HasMemberFailure<Scratch>);
  static_assert(!HasMemberCycleWalk<Scratch>);
  static_assert(!HasMemberOnWalk<Scratch>);
  static_assert(!HasMemberWalked<Scratch>);
  static_assert(!HasMemberCommitted<Scratch>);
  static_assert(!HasMemberCommittedFrom<Scratch>);

  // What the type does promise: pre-sizing, and the value semantics it had
  // as an aggregate of vectors. A private section removes neither, but it
  // removes them silently if a future edit adds a member that does.
  static_assert(
      requires(Scratch& scratch) { scratch.reserve(std::size_t{4}); });
  static_assert(std::is_default_constructible_v<Scratch>);
  static_assert(std::is_copy_constructible_v<Scratch>);
  static_assert(std::is_copy_assignable_v<Scratch>);
  static_assert(std::is_move_constructible_v<Scratch>);
  static_assert(std::is_move_assignable_v<Scratch>);

  // Deliberate: aggregate and designated initialization of the buffers is
  // exactly the surface being withdrawn.
  static_assert(!std::is_aggregate_v<Scratch>);

  SUCCEED();
}

TEST(TessJointMovement, WeightedTickDriverResolvesACorridorHeadOn) {
  // End to end through planning: two agents with crossed goals in a one-wide
  // corridor. Under Forbid they wedge (the recorded colony failure class);
  // under Permit they exchange mid-corridor and both arrive.
  const auto build = [](World& world, std::vector<tess::PathAgentState>& agents,
                        tess::PathAgentTickState& state) {
    fill_world(world);
    for (int x = 0; x < 32; ++x) {
      for (int y = 0; y < 32; ++y) {
        if (y != 1) {
          world.field<PassableTag>(tess::Coord3{x, y, 0}) = false;
        }
      }
    }
    tess::PathAgentState a;
    a.position = {1, 1, 0};
    world.field<OccupancyTag>(a.position) = true;
    tess::set_path_agent_goal(state, a, {6, 1, 0});
    agents.push_back(a);
    tess::PathAgentState b;
    b.position = {6, 1, 0};
    world.field<OccupancyTag>(b.position) = true;
    tess::set_path_agent_goal(state, b, {1, 1, 0});
    agents.push_back(b);
  };

  const auto arrived = [](const std::vector<tess::PathAgentState>& agents) {
    return !agents[0].has_goal && !agents[1].has_goal;
  };

  for (const auto policy :
       {tess::SwapPolicy::Forbid, tess::SwapPolicy::Permit}) {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentTickState state;
    build(world, agents, state);
    tess::PathRequestRuntime runtime;
    runtime.reserve_requests(4);
    runtime.reserve_search_nodes(4096);
    runtime.reserve_path_nodes(1024);
    tess::JointMoveScratch scratch;
    scratch.reserve(agents.size());
    auto options = tess::PathAgentTickOptions{};
    options.max_blocked_retries = 64;

    for (int tick = 0; tick < 32 && !arrived(agents); ++tick) {
      (void)tess::tick_weighted_path_agents_with_joint_movement<
          World, Walker, 4u, OccupancyTag, ReservationTag>(
          state, world, agents, runtime, scratch, options,
          tess::JointMoveOptions{policy});
    }
    if (policy == tess::SwapPolicy::Permit) {
      EXPECT_TRUE(arrived(agents));
    } else {
      EXPECT_FALSE(arrived(agents));  // the wedge Forbid deliberately keeps
    }
  }
}

}  // namespace

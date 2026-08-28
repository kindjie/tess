#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};
struct SettledTag {};

using Pibt2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>,
    tess::Field<SettledTag, bool>>;
using World = tess::AlwaysResidentWorld<Pibt2D, Schema>;
using Walker =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;
using Traveler = tess::movement::MovementClass<
    tess::movement::AllOf<
        tess::movement::Field<PassableTag>,
        tess::movement::Not<tess::movement::Field<SettledTag>>>,
    tess::movement::FieldCost<CostTag>>;

// Library-scale world for the ring regression: the same lattice the Phase 3
// gate evaluation ran on.
using Ring2D = tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;
using RingWorld = tess::AlwaysResidentWorld<Ring2D, Schema>;

template <typename TWorld>
void fill_world(TWorld& world, bool passable) {
  for (auto& page : world.chunks()) {
    auto open = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    auto occupancy = page.template field_span<OccupancyTag>();
    auto reservation = page.template field_span<ReservationTag>();
    auto settled = page.template field_span<SettledTag>();
    for (std::size_t i = 0; i < open.size(); ++i) {
      open[i] = passable;
      cost[i] = 1u;
      occupancy[i] = false;
      reservation[i] = false;
      settled[i] = false;
    }
  }
}

auto add_agent(World& world, std::vector<tess::PathAgentState>& agents,
               tess::PathAgentRoutes& routes, std::vector<tess::Coord3> route)
    -> std::size_t {
  tess::PathAgentState agent;
  agent.position = route.front();
  agent.goal = route.back();
  agent.has_goal = true;
  agent.last_result = tess::PathStatus::Found;
  agent.phase = tess::PathAgentPhase::Following;
  world.field<OccupancyTag>(agent.position) = true;
  agents.push_back(agent);
  routes.routes.push_back(std::move(route));
  return agents.size() - 1;
}

// A test-local breadth-first distance table over an arbitrary passability
// predicate, used both as the ranking oracle and to demonstrate the
// oracle-consistency contract. `width` is the square lattice extent.
template <typename TWorld, typename Passable>
auto bfs_distances(const TWorld& world, int width, tess::Coord3 goal,
                   Passable&& passable) -> std::vector<std::uint32_t> {
  constexpr auto unreachable = std::numeric_limits<std::uint32_t>::max();
  const auto span = static_cast<std::size_t>(width);
  std::vector<std::uint32_t> distance(span * span, unreachable);
  const auto index = [span](tess::Coord3 c) {
    return static_cast<std::size_t>(c.y) * span + static_cast<std::size_t>(c.x);
  };
  std::vector<tess::Coord3> frontier;
  if (!passable(world, goal)) {
    return distance;
  }
  distance[index(goal)] = 0;
  frontier.push_back(goal);
  for (std::size_t head = 0; head < frontier.size(); ++head) {
    const auto current = frontier[head];
    const auto next = distance[index(current)] + 1;
    for (const auto neighbour : {tess::Coord3{current.x + 1, current.y, 0},
                                 tess::Coord3{current.x - 1, current.y, 0},
                                 tess::Coord3{current.x, current.y + 1, 0},
                                 tess::Coord3{current.x, current.y - 1, 0}}) {
      if (neighbour.x < 0 || neighbour.y < 0 || neighbour.x >= width ||
          neighbour.y >= width || !passable(world, neighbour)) {
        continue;
      }
      if (distance[index(neighbour)] <= next) {
        continue;
      }
      distance[index(neighbour)] = next;
      frontier.push_back(neighbour);
    }
  }
  return distance;
}

constexpr auto terrain_passable = [](const auto& world, tess::Coord3 c) {
  return world.template field<PassableTag>(c);
};

constexpr auto traveler_passable = [](const auto& world, tess::Coord3 c) {
  return world.template field<PassableTag>(c) &&
         !world.template field<SettledTag>(c);
};

TEST(TessPibtMovement, SaturatedCursorClassifiesTheCommitAsOffRoute) {
  // The commit classifier asks whether the landed tile is the route's
  // next step. With `path_index` saturated, `path_index + 1` wraps to
  // zero, so a move onto route[0] -- here, a step back toward the goal
  // -- would be classified as on-route and the cursor incremented to
  // zero, restarting a route the cursor says is consumed. The guarded
  // form classifies it off-route: the move still commits, and the stale
  // route is dropped for replanning.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, true);
  const auto i =
      add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
  agents[i].position = tess::Coord3{2, 1, 0};
  agents[i].goal = tess::Coord3{0, 1, 0};  // route[0] is the best step
  world.field<OccupancyTag>(tess::Coord3{1, 1, 0}) = false;
  world.field<OccupancyTag>(agents[i].position) = true;
  agents[i].path_index = std::numeric_limits<std::size_t>::max();

  tess::PibtPriorities priorities;
  priorities.elapsed = {1};
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal = agents[agent].goal;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };
  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);

  EXPECT_EQ(stats.frame.advanced, 1u);
  EXPECT_EQ(agents[i].position, (tess::Coord3{1, 1, 0}));
  // Off-route commit: the retained route is dropped rather than resumed
  // with a wrapped cursor.
  EXPECT_EQ(agents[i].path_index, std::numeric_limits<std::size_t>::max());
  EXPECT_FALSE(agents[i].last_result.has_value());
}

TEST(TessPibtMovement, RingRegressionPibtSolvesWhereJointCongestionSeals) {
  // A Phase 3 gate cell (width-3 ring, 64x64, n=48, seeded) through the real
  // tick driver with the full settled consumer recipe. On this seed the joint
  // commit's slower congestion resolution lets arrivals settle into a seal
  // that cuts one agent's goal off outright — verified below by a
  // class-consistent BFS from the goal — while the PIBT tier keeps the
  // population moving so the same settle order never forms and every agent
  // arrives. Sealing, not patience, is the joint failure mode here: the gate
  // sweep showed stranded-but-reachable residuals are what PIBT eliminates
  // (30 -> 5 at width 2, 2 -> 0 at width 3), while sealed instances are
  // unsolvable for any movement tier. If a joint improvement ever solves this
  // seed, re-run the sweep and pin a new discriminating seed rather than
  // deleting the tier.
  struct Rng {
    std::uint64_t s;
    auto next() -> std::uint32_t {
      s ^= s << 13U;
      s ^= s >> 7U;
      s ^= s << 17U;
      return static_cast<std::uint32_t>(s >> 32U);
    }
    auto in(int lo, int hi) -> int {
      return lo +
             static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1));
    }
  };

  const auto build = [](RingWorld& world,
                        std::vector<tess::PathAgentState>& agents,
                        tess::PathAgentTickState& state, std::uint64_t seed) {
    fill_world(world, false);
    for (int y = 1; y < 63; ++y) {
      for (int x = 1; x < 63; ++x) {
        if (x < 4 || x >= 60 || y < 4 || y >= 60) {
          world.field<PassableTag>(tess::Coord3{x, y, 0}) = true;
        }
      }
    }
    std::vector<tess::Coord3> free;
    for (int y = 1; y < 63; ++y) {
      for (int x = 1; x < 63; ++x) {
        if (world.field<PassableTag>(tess::Coord3{x, y, 0})) {
          free.push_back({x, y, 0});
        }
      }
    }
    std::vector<tess::Coord3> goal_pool = free;
    Rng rng{seed};
    for (std::size_t i = free.size(); i > 1; --i) {
      const auto pick =
          static_cast<std::size_t>(rng.in(0, static_cast<int>(i) - 1));
      std::swap(free[i - 1], free[pick]);
    }
    for (std::size_t i = goal_pool.size(); i > 1; --i) {
      const auto pick =
          static_cast<std::size_t>(rng.in(0, static_cast<int>(i) - 1));
      std::swap(goal_pool[i - 1], goal_pool[pick]);
    }
    constexpr int n = 48;
    for (int i = 0; i < n; ++i) {
      tess::PathAgentState agent;
      agent.position = free[static_cast<std::size_t>(i)];
      world.field<OccupancyTag>(agent.position) = true;
      tess::set_path_agent_goal(state, agent,
                                goal_pool[static_cast<std::size_t>(i)]);
      agents.push_back(agent);
    }
  };

  const auto refresh_settled = [](RingWorld& world,
                                  std::vector<tess::PathAgentState>& agents) {
    bool changed = false;
    for (auto& agent : agents) {
      const bool settled =
          !agent.has_goal || agent.phase == tess::PathAgentPhase::Unreachable;
      if ((world.field<SettledTag>(agent.position) != 0) == settled) {
        continue;
      }
      world.field<SettledTag>(agent.position) = settled;
      const auto key =
          tess::chunk_key<Ring2D>(tess::chunk_coord<Ring2D>(agent.position));
      world.mark_dirty(key, tess::DirtyMask{1u << 1u},
                       tess::Box3{agent.position, tess::Extent3{1, 1, 1}});
      world.clear_dirty(key, tess::DirtyMask{1u << 1u});
      changed = true;
    }
    return changed;
  };

  const auto all_arrived = [](const std::vector<tess::PathAgentState>& agents) {
    for (const auto& agent : agents) {
      if (agent.has_goal) {
        return false;
      }
    }
    return true;
  };

  // Trial 18 of the gate sweep's seed schedule.
  constexpr std::uint64_t kSeed = 0x9E3779B97F4A7C15ULL * 19u;
  constexpr int kCap = 1500;

  {
    RingWorld world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentTickState state;
    build(world, agents, state, kSeed);
    tess::PathRequestRuntime runtime;
    runtime.reserve_requests(128);
    runtime.reserve_search_nodes(16384);
    runtime.reserve_path_nodes(65536);
    tess::JointMoveScratch scratch;
    scratch.reserve(agents.size());
    auto options = tess::PathAgentTickOptions{};
    options.max_blocked_retries = 1u << 20u;  // patience is not the problem
    for (int tick = 0; tick < kCap && !all_arrived(agents); ++tick) {
      (void)refresh_settled(world, agents);
      (void)tess::tick_weighted_path_agents_with_joint_movement<
          RingWorld, Traveler, 4u, OccupancyTag, ReservationTag>(
          state, world, agents, runtime, scratch, options,
          tess::JointMoveOptions{tess::SwapPolicy::Permit});
    }
    EXPECT_FALSE(all_arrived(agents));
    // The stranding is a seal: the residual agent's goal is unreachable under
    // the final settled set, so the failure is the settle order the joint
    // commit produced, not missing patience.
    for (const auto& agent : agents) {
      if (!agent.has_goal) {
        continue;
      }
      const auto table =
          bfs_distances(world, 64, agent.goal, traveler_passable);
      EXPECT_EQ(table[static_cast<std::size_t>(agent.position.y) * 64u +
                      static_cast<std::size_t>(agent.position.x)],
                std::numeric_limits<std::uint32_t>::max());
    }
  }

  {
    RingWorld world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentTickState state;
    build(world, agents, state, kSeed);
    tess::PathRequestRuntime runtime;
    runtime.reserve_requests(128);
    runtime.reserve_search_nodes(16384);
    runtime.reserve_path_nodes(65536);
    tess::JointMoveScratch scratch;
    scratch.reserve(agents.size());
    tess::PibtPriorities priorities;
    priorities.reserve(agents.size());
    auto options = tess::PathAgentTickOptions{};
    options.max_blocked_retries = 1u << 20u;

    // Per-agent oracles over the class the agents move with, rebuilt when the
    // settled set changes (the oracle-consistency contract).
    std::vector<std::vector<std::uint32_t>> tables(agents.size());
    const auto rebuild = [&](RingWorld& w) {
      for (std::size_t i = 0; i < agents.size(); ++i) {
        const auto goal =
            agents[i].has_goal ? agents[i].goal : agents[i].position;
        tables[i] = bfs_distances(w, 64, goal, traveler_passable);
      }
    };
    rebuild(world);
    const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
      return tables[agent][static_cast<std::size_t>(c.y) * 64u +
                           static_cast<std::size_t>(c.x)];
    };

    for (int tick = 0; tick < kCap && !all_arrived(agents); ++tick) {
      if (refresh_settled(world, agents)) {
        rebuild(world);
      }
      (void)tess::tick_weighted_path_agents_with_pibt<
          RingWorld, Traveler, 4u, OccupancyTag, ReservationTag>(
          state, world, agents, runtime, priorities, scratch, rank, options,
          tess::JointMoveOptions{tess::SwapPolicy::Permit});
    }
    EXPECT_TRUE(all_arrived(agents));
  }
}

TEST(TessPibtMovement, InheritanceYieldsAnOffRouteDetourForbidJointCannot) {
  // The capability difference in its purest form, fully deterministic: a
  // dead-end corridor head-on where resolution requires the blocked agent to
  // step OFF its route into a side pocket. The joint commit only admits
  // moves along retained routes, so under Forbid it wedges forever
  // (patience-invariant); PIBT's priority inheritance hands the turn to the
  // blocker, which yields into the pocket, without needing swaps at all.
  //
  //   corridor: (3,1)-(5,1); pocket row: (3,2)-(4,2)
  //   A at (3,1) -> (5,1); B at (4,1) -> (3,1)
  //
  // Every ranking choice below is strict (no ties), so the outcome does not
  // depend on the transition model's enumeration order.
  const auto build = [](World& world, std::vector<tess::PathAgentState>& agents,
                        tess::PathAgentRoutes& routes) {
    fill_world(world, false);
    for (const auto c :
         {tess::Coord3{3, 1, 0}, tess::Coord3{4, 1, 0}, tess::Coord3{5, 1, 0},
          tess::Coord3{3, 2, 0}, tess::Coord3{4, 2, 0}}) {
      world.field<PassableTag>(c) = true;
    }
    add_agent(world, agents, routes, {{3, 1, 0}, {4, 1, 0}, {5, 1, 0}});
    add_agent(world, agents, routes, {{4, 1, 0}, {3, 1, 0}});
  };

  {
    // Fail-before: the joint commit under Forbid never resolves this.
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    tess::JointMoveScratch scratch;
    for (int tick = 0; tick < 32; ++tick) {
      const auto stats = tess::advance_path_agents_with_joint_movement<
          World, PassableTag, OccupancyTag, ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, scratch);
      EXPECT_EQ(stats.frame.advanced, 0u);
    }
    EXPECT_EQ(agents[0].position, (tess::Coord3{3, 1, 0}));
    EXPECT_EQ(agents[1].position, (tess::Coord3{4, 1, 0}));
  }

  {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    tess::PibtPriorities priorities;
    priorities.elapsed = {0, 100};  // B decides first and inherits into A
    tess::JointMoveScratch scratch;
    const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
      const auto goal =
          agents[agent].has_goal ? agents[agent].goal : agents[agent].position;
      const auto table = bfs_distances(world, 32, goal, terrain_passable);
      return table[static_cast<std::size_t>(c.y) * 32u +
                   static_cast<std::size_t>(c.x)];
    };
    bool both_arrived = false;
    bool saw_route_invalidation = false;
    for (int tick = 0; tick < 16 && !both_arrived; ++tick) {
      (void)tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                                ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);  // Forbid: no swaps needed, only yielding
      saw_route_invalidation |= std::ranges::any_of(agents, [](const auto& a) {
        return a.phase == tess::PathAgentPhase::Blocked &&
               !a.last_result.has_value();
      });
      both_arrived = !agents[0].has_goal && !agents[1].has_goal;
    }
    EXPECT_TRUE(saw_route_invalidation);
    EXPECT_TRUE(both_arrived);
    EXPECT_EQ(agents[0].position, (tess::Coord3{5, 1, 0}));
    EXPECT_EQ(agents[1].position, (tess::Coord3{3, 1, 0}));
  }
}

TEST(TessPibtMovement, RankingOracleMustShareMovementClassPassability) {
  // Screening defect #4 as a library test: a corridor with a settled blocker
  // and a detour row. A terrain-only oracle rates staying beside the blocker
  // above the detour, and the agent parks forever; a class-consistent oracle
  // detours.
  const auto build = [](World& world, std::vector<tess::PathAgentState>& agents,
                        tess::PathAgentRoutes& routes) {
    fill_world(world, false);
    for (int x = 1; x <= 8; ++x) {
      world.field<PassableTag>(tess::Coord3{x, 1, 0}) = true;
      world.field<PassableTag>(tess::Coord3{x, 2, 0}) = true;
    }
    add_agent(world, agents, routes, {{1, 1, 0}, {8, 1, 0}});
    // A settled peer mid-corridor.
    world.field<OccupancyTag>(tess::Coord3{4, 1, 0}) = true;
    world.field<SettledTag>(tess::Coord3{4, 1, 0}) = true;
  };

  for (const bool consistent : {false, true}) {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    const auto table =
        consistent ? bfs_distances(world, 32, {8, 1, 0}, traveler_passable)
                   : bfs_distances(world, 32, {8, 1, 0}, terrain_passable);
    const auto rank = [&](std::size_t, tess::Coord3 c) -> std::uint32_t {
      return table[static_cast<std::size_t>(c.y) * 32u +
                   static_cast<std::size_t>(c.x)];
    };
    tess::PibtPriorities priorities;
    tess::JointMoveScratch scratch;
    for (int tick = 0; tick < 24 && agents[0].has_goal; ++tick) {
      (void)tess::advance_path_agents_with_pibt<World, Traveler, OccupancyTag,
                                                ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);
    }
    if (consistent) {
      EXPECT_FALSE(agents[0].has_goal);  // detoured through row 2 and arrived
    } else {
      EXPECT_TRUE(agents[0].has_goal);  // parked beside the obstruction
      EXPECT_EQ(agents[0].position, (tess::Coord3{3, 1, 0}));
    }
  }
}

TEST(TessPibtMovement, PriorityInheritanceDisplacesALowerPriorityBlocker) {
  // The high-priority agent's best tile is held by an idle-but-active peer;
  // the peer inherits the turn and steps aside the same tick.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, true);
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
  add_agent(world, agents, routes, {{2, 1, 0}, {2, 1, 0}});  // parked, active
  agents[1].goal = {2, 1, 0};

  tess::PibtPriorities priorities;
  priorities.elapsed = {100, 0};  // the mover decides first
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal =
        agents[agent].has_goal ? agents[agent].goal : agents[agent].position;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };
  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);
  EXPECT_EQ(stats.frame.advanced, 2u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{2, 1, 0}));
  EXPECT_FALSE(agents[1].position == (tess::Coord3{2, 1, 0}));
}

TEST(TessPibtMovement, BacktrackingTriesTheNextCandidateWhenAPeerIsBoxed) {
  // The best candidate's occupant cannot move anywhere (boxed by walls); the
  // chooser must fall back to its next-ranked candidate instead of wedging.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, false);
  // A cross: mover at the centre-left, boxed peer in a one-tile pocket.
  for (const auto c : {tess::Coord3{1, 2, 0}, tess::Coord3{2, 2, 0},
                       tess::Coord3{3, 2, 0}, tess::Coord3{2, 1, 0}}) {
    world.field<PassableTag>(c) = true;
  }
  add_agent(world, agents, routes, {{1, 2, 0}, {2, 1, 0}});  // prefers (2,1)
  add_agent(world, agents, routes, {{2, 1, 0}, {2, 1, 0}});  // pocket peer
  agents[1].goal = {2, 1, 0};

  tess::PibtPriorities priorities;
  priorities.elapsed = {100, 0};
  tess::JointMoveScratch scratch;
  // Rank (2,1) best for the mover, (2,2) second; the pocket peer has no free
  // neighbour once the mover's own tile is spoken for.
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    if (agent == 0) {
      if (c == tess::Coord3{2, 1, 0}) {
        return 0;
      }
      if (c == tess::Coord3{2, 2, 0}) {
        return 1;
      }
      return 5;
    }
    return c == agents[1].position ? 0 : 1;
  };
  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);
  EXPECT_EQ(agents[0].position, (tess::Coord3{2, 2, 0}));
  EXPECT_EQ(agents[1].position, (tess::Coord3{2, 1, 0}));
  EXPECT_EQ(stats.frame.advanced, 1u);
}

TEST(TessPibtMovement, FailedPeerKeepsItsClaimSoLaterDecidersCannotStack) {
  // Backtracking must not release the claim on a failed peer's tile: the
  // peer stays there, so a later decider that ranks the same tile first has
  // to be vertex-rejected, not admitted on top of it. B is boxed (its only
  // exits are A's tile — an edge conflict under Forbid — and its own tile,
  // claimed by A), so A's inheritance into B fails and A backtracks; C then
  // wants B's tile and must be refused.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, false);
  for (const auto c :
       {tess::Coord3{1, 1, 0}, tess::Coord3{2, 1, 0}, tess::Coord3{2, 2, 0}}) {
    world.field<PassableTag>(c) = true;
  }
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});  // A
  add_agent(world, agents, routes, {{2, 1, 0}, {2, 1, 0}});  // B, boxed
  add_agent(world, agents, routes, {{2, 2, 0}, {2, 1, 0}});  // C
  // C's own tile is reserved (application-owned do-not-enter), so B never
  // enumerates it and cannot inherit into C; C stays undecided until after
  // A's backtrack.
  world.field<ReservationTag>(tess::Coord3{2, 2, 0}) = true;

  tess::PibtPriorities priorities;
  priorities.elapsed = {100, 0, 50};  // decision order: A, C, B
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal = agents[agent].goal;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };
  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);
  EXPECT_EQ(stats.frame.advanced, 0u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 1, 0}));
  EXPECT_EQ(agents[1].position, (tess::Coord3{2, 1, 0}));
  EXPECT_EQ(agents[2].position, (tess::Coord3{2, 2, 0}));
}

TEST(TessPibtMovement, ExternalOccupantsAreNeverEntered) {
  // Occupancy owned by anything outside the agent span (another population,
  // an application marker) must block admission exactly as in the joint
  // pass; the occupant index only knows span agents, so the world bit is
  // the authority.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, false);
  for (const auto c :
       {tess::Coord3{1, 1, 0}, tess::Coord3{2, 1, 0}, tess::Coord3{3, 1, 0}}) {
    world.field<PassableTag>(c) = true;
  }
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
  world.field<OccupancyTag>(tess::Coord3{2, 1, 0}) = true;  // external

  tess::PibtPriorities priorities;
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t, tess::Coord3 c) -> std::uint32_t {
    return static_cast<std::uint32_t>(std::abs(c.x - 3) + std::abs(c.y - 1));
  };
  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);
  EXPECT_EQ(stats.frame.advanced, 0u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{1, 1, 0}));
  EXPECT_TRUE(world.field<OccupancyTag>(tess::Coord3{2, 1, 0}));
}

TEST(TessPibtMovement, ImpassableSourceFailsImpassableFrom) {
  // Terrain changed under a standing agent: `commit_movement_intent` fails
  // `ImpassableFrom`, and so must PIBT — the agent neither moves nor yields to
  // inheritance.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, true);
  add_agent(world, agents, routes, {{2, 2, 0}, {3, 2, 0}});
  world.field<PassableTag>(tess::Coord3{2, 2, 0}) = false;

  tess::PibtPriorities priorities;
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t, tess::Coord3 c) -> std::uint32_t {
    return static_cast<std::uint32_t>(std::abs(c.x - 3) + std::abs(c.y - 2));
  };
  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);
  EXPECT_EQ(stats.frame.advanced, 0u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{2, 2, 0}));
  EXPECT_EQ(stats.frame.movement_failures.impassable, 1u);
  EXPECT_EQ(stats.frame.movement_failures.occupied, 0u);
}

TEST(TessPibtMovement, MaxStepsPausesAndMultiSteps) {
  const auto build = [](World& world, std::vector<tess::PathAgentState>& agents,
                        tess::PathAgentRoutes& routes) {
    fill_world(world, true);
    add_agent(world, agents, routes,
              {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}, {4, 1, 0}});
  };
  const auto rank = [](std::vector<tess::PathAgentState>& agents) {
    return [&agents](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
      const auto goal = agents[agent].goal;
      return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                        std::abs(c.y - goal.y));
    };
  };

  {
    // Zero steps is paused movement, as in the joint advance.
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    tess::PibtPriorities priorities;
    tess::JointMoveScratch scratch;
    const auto stats =
        tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                            ReservationTag>(
            world, std::span<tess::PathAgentState>(agents), routes, priorities,
            scratch, rank(agents), {}, {.max_steps = 0});
    EXPECT_EQ(stats.frame.advanced, 0u);
    EXPECT_EQ(agents[0].position, (tess::Coord3{1, 1, 0}));
  }
  {
    // Two steps advance two route tiles in one call and stop early once the
    // goal is reached.
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    tess::PibtPriorities priorities;
    tess::JointMoveScratch scratch;
    const auto stats =
        tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                            ReservationTag>(
            world, std::span<tess::PathAgentState>(agents), routes, priorities,
            scratch, rank(agents), {}, {.max_steps = 2});
    EXPECT_EQ(stats.frame.advanced, 2u);
    EXPECT_EQ(agents[0].position, (tess::Coord3{3, 1, 0}));
    EXPECT_EQ(agents[0].path_index, 2u);
  }
}

TEST(TessPibtMovement, SwapCounterOnlyCountsSecuredExchanges) {
  // C -> A -> B in span order with C deciding first: C claims A's tile, A
  // inherits and claims B's tile, and B's policy-allowed reverse edge onto
  // A's tile then loses the vertex claim to C. No exchange happens, so the
  // swap counter must stay at zero.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, true);
  add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});  // C
  add_agent(world, agents, routes, {{2, 1, 0}, {3, 1, 0}});  // A
  add_agent(world, agents, routes, {{3, 1, 0}, {2, 1, 0}});  // B

  tess::PibtPriorities priorities;
  priorities.elapsed = {100, 50, 0};  // C, then A, then B
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal = agents[agent].goal;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };
  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank, tess::JointMoveOptions{tess::SwapPolicy::Permit});
  EXPECT_EQ(stats.swaps, 0u);
  EXPECT_EQ(agents[0].position, (tess::Coord3{2, 1, 0}));
  EXPECT_EQ(agents[1].position, (tess::Coord3{3, 1, 0}));
  // B yielded somewhere other than the contested pair of tiles.
  EXPECT_FALSE(agents[2].position == (tess::Coord3{2, 1, 0}));
  EXPECT_FALSE(agents[2].position == (tess::Coord3{3, 1, 0}));
}

TEST(TessPibtMovement, ArrivalResetsPriorityBeforeAnyNewGoal) {
  // An arrival must reset its adaptive priority at the commit itself: if
  // the application assigns the agent a new goal before the next pass, the
  // stale `elapsed` from the finished journey would otherwise outrank
  // agents that have genuinely waited longer.
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, true);
  add_agent(world, agents, routes, {{2, 2, 0}, {2, 1, 0}});  // A, arriving
  add_agent(world, agents, routes,
            {{4, 0, 0}, {3, 0, 0}, {2, 0, 0}});  // B, en route

  tess::PibtPriorities priorities;
  priorities.elapsed = {100, 50};
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal =
        agents[agent].has_goal ? agents[agent].goal : agents[agent].position;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };

  // A arrives; its priority resets at the commit. B advances toward (2,0).
  (void)tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                            ReservationTag>(
      world, std::span<tess::PathAgentState>(agents), routes, priorities,
      scratch, rank);
  ASSERT_FALSE(agents[0].has_goal);
  ASSERT_EQ(agents[0].position, (tess::Coord3{2, 1, 0}));
  ASSERT_EQ(agents[1].position, (tess::Coord3{3, 0, 0}));
  EXPECT_EQ(priorities.elapsed[0], 0u);

  // The application immediately hands A a new goal contending with B for
  // (2,0). A and B are not adjacent to each other, only to the contested
  // tile, so the higher priority simply claims it first: B's accumulated
  // wait must outrank A's reset one.
  agents[0].goal = {2, 0, 0};
  agents[0].has_goal = true;
  agents[0].last_result = tess::PathStatus::Found;
  agents[0].phase = tess::PathAgentPhase::Following;
  routes.routes[0] = {agents[0].position, {2, 0, 0}};
  agents[0].path_index = 0;
  (void)tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                            ReservationTag>(
      world, std::span<tess::PathAgentState>(agents), routes, priorities,
      scratch, rank);
  EXPECT_EQ(agents[1].position, (tess::Coord3{2, 0, 0}));
  EXPECT_EQ(agents[0].position, (tess::Coord3{2, 1, 0}));
}

TEST(TessPibtMovement, HeadOnPairFollowsSwapPolicy) {
  const auto build = [](World& world, std::vector<tess::PathAgentState>& agents,
                        tess::PathAgentRoutes& routes) {
    fill_world(world, false);
    world.field<PassableTag>(tess::Coord3{1, 1, 0}) = true;
    world.field<PassableTag>(tess::Coord3{2, 1, 0}) = true;
    add_agent(world, agents, routes, {{1, 1, 0}, {2, 1, 0}});
    add_agent(world, agents, routes, {{2, 1, 0}, {1, 1, 0}});
  };
  const auto rank = [](std::vector<tess::PathAgentState>& agents) {
    return [&agents](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
      const auto goal = agents[agent].goal;
      return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                        std::abs(c.y - goal.y));
    };
  };

  {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    tess::PibtPriorities priorities;
    tess::JointMoveScratch scratch;
    const auto stats =
        tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                            ReservationTag>(
            world, std::span<tess::PathAgentState>(agents), routes, priorities,
            scratch, rank(agents));  // Forbid
    EXPECT_EQ(stats.frame.advanced, 0u);
    EXPECT_GE(stats.swaps_denied, 1u);
  }
  {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    build(world, agents, routes);
    tess::PibtPriorities priorities;
    tess::JointMoveScratch scratch;
    const auto stats =
        tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                            ReservationTag>(
            world, std::span<tess::PathAgentState>(agents), routes, priorities,
            scratch, rank(agents),
            tess::JointMoveOptions{tess::SwapPolicy::Permit});
    EXPECT_EQ(stats.frame.advanced, 2u);
    EXPECT_EQ(stats.swaps, 1u);
    EXPECT_EQ(agents[0].position, (tess::Coord3{2, 1, 0}));
    EXPECT_EQ(agents[1].position, (tess::Coord3{1, 1, 0}));
  }
}

TEST(TessPibtMovement, DeterministicAcrossIdenticalRuns) {
  const auto run = [](std::vector<tess::Coord3>& positions) {
    World world;
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    fill_world(world, true);
    add_agent(world, agents, routes, {{1, 1, 0}, {5, 5, 0}});
    add_agent(world, agents, routes, {{5, 5, 0}, {1, 1, 0}});
    add_agent(world, agents, routes, {{3, 3, 0}, {3, 1, 0}});
    tess::PibtPriorities priorities;
    tess::JointMoveScratch scratch;
    const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
      const auto goal = agents[agent].goal;
      return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                        std::abs(c.y - goal.y));
    };
    for (int tick = 0; tick < 16; ++tick) {
      (void)tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                                ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank, tess::JointMoveOptions{tess::SwapPolicy::Permit});
    }
    positions.clear();
    for (const auto& agent : agents) {
      positions.push_back(agent.position);
    }
  };
  std::vector<tess::Coord3> first;
  std::vector<tess::Coord3> second;
  run(first);
  run(second);
  EXPECT_EQ(first, second);
}

TEST(TessPibtMovement, WarmPassAllocatesNothingAfterReserve) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, true);
  for (int i = 0; i < 8; ++i) {
    add_agent(world, agents, routes, {{1 + i, 1, 0}, {1 + i, 20, 0}});
  }
  tess::PibtPriorities priorities;
  priorities.reserve(agents.size());
  tess::JointMoveScratch scratch;
  scratch.reserve(agents.size());
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal = agents[agent].goal;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };
  (void)tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                            ReservationTag>(
      world, std::span<tess::PathAgentState>(agents), routes, priorities,
      scratch, rank);  // warm

  tess_test::ScopedAllocationCounter counter;
  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);
  EXPECT_EQ(counter.count(), 0u);
  EXPECT_EQ(counter.bytes(), 0u);
  EXPECT_EQ(stats.frame.advanced, 8u);
}

TEST(TessPibtMovement, DistanceAtReadsTheProductAndGuardsShape) {
  World world;
  fill_world(world, true);
  tess::GoalSet goals;
  goals.add({5, 5, 0});
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  const auto built = tess::build_distance_field_product<World, PassableTag>(
      world, goals, product, scratch);
  ASSERT_EQ(built.status, tess::PathStatus::Found);

  EXPECT_EQ(product.distance_at<World>({5, 5, 0}), 0u);
  EXPECT_EQ(product.distance_at<World>({6, 5, 0}), 1u);
  EXPECT_EQ(product.distance_at<World>({5, 8, 0}), 3u);
  // Out of shape: sentinel, not undefined behaviour.
  EXPECT_EQ(product.distance_at<World>({40, 40, 0}),
            tess::DistanceFieldProduct::unreachable_distance);

  // A world type with a different shape must not read the array.
  using OtherShape =
      tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{8, 8, 1}>;
  using OtherWorld = tess::AlwaysResidentWorld<OtherShape, Schema>;
  EXPECT_EQ(product.distance_at<OtherWorld>({5, 5, 0}),
            tess::DistanceFieldProduct::unreachable_distance);

  // An unreached tile (walled off after build is the caller's staleness
  // problem; here: build with a wall so the pocket is never reached).
  World walled;
  fill_world(walled, true);
  for (int y = 0; y < 32; ++y) {
    walled.field<PassableTag>(tess::Coord3{16, y, 0}) = false;
  }
  tess::DistanceFieldProduct pocket;
  const auto rebuilt = tess::build_distance_field_product<World, PassableTag>(
      walled, goals, pocket, scratch);
  ASSERT_EQ(rebuilt.status, tess::PathStatus::Found);
  EXPECT_EQ(pocket.distance_at<World>({20, 5, 0}),
            tess::DistanceFieldProduct::unreachable_distance);
}

// An agent that has arrived (`has_goal` cleared) or ended at `Unreachable`
// is immovable. The priority loop skips such agents and the apply pass tests
// the same condition before touching a stay-put agent, but priority
// inheritance reached them through the occupant path and shoved them aside,
// rewriting a terminal lifecycle back to `Blocked`.
TEST(TessPibtMovement, InheritanceDoesNotDisplaceAnArrivedAgent) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, false);
  // Corridor (1,1)-(3,1) with a free pocket at (2,2) the settled agent
  // could be pushed into if inheritance were allowed to move it.
  for (const auto c : {tess::Coord3{1, 1, 0}, tess::Coord3{2, 1, 0},
                       tess::Coord3{3, 1, 0}, tess::Coord3{2, 2, 0}}) {
    world.field<PassableTag>(c) = true;
  }
  add_agent(world, agents, routes, {{1, 1, 0}, {3, 1, 0}});  // mover
  add_agent(world, agents, routes, {{2, 1, 0}, {2, 1, 0}});  // settled

  // The settled agent arrived: the library's own goal-clearing helper
  // leaves it goalless and Idle, which is the state a real arrival reaches.
  tess::clear_path_agent_goal(agents[1]);

  tess::PibtPriorities priorities;
  priorities.elapsed = {100, 0};  // the mover decides first
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal =
        agents[agent].has_goal ? agents[agent].goal : agents[agent].position;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };

  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);

  // The settled agent keeps its tile and its terminal lifecycle.
  EXPECT_EQ(agents[1].position, (tess::Coord3{2, 1, 0}));
  EXPECT_FALSE(agents[1].has_goal);
  EXPECT_EQ(agents[1].phase, tess::PathAgentPhase::Idle);
  EXPECT_EQ(agents[1].blocked_retries, 0u);
  // And the mover never lands on the tile it could not clear.
  EXPECT_FALSE(agents[0].position == (tess::Coord3{2, 1, 0}));
  EXPECT_EQ(stats.frame.arrived, 0u);
}

TEST(TessPibtMovement, InheritanceDoesNotDisplaceAnUnreachableAgent) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, false);
  for (const auto c : {tess::Coord3{1, 1, 0}, tess::Coord3{2, 1, 0},
                       tess::Coord3{3, 1, 0}, tess::Coord3{2, 2, 0}}) {
    world.field<PassableTag>(c) = true;
  }
  add_agent(world, agents, routes, {{1, 1, 0}, {3, 1, 0}});
  add_agent(world, agents, routes, {{2, 1, 0}, {3, 1, 0}});

  // Terminal until a new goal is assigned, per the path-agent contract.
  agents[1].last_result = tess::PathStatus::NoPath;
  agents[1].phase = tess::PathAgentPhase::Unreachable;

  tess::PibtPriorities priorities;
  priorities.elapsed = {100, 0};
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal =
        agents[agent].has_goal ? agents[agent].goal : agents[agent].position;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };

  (void)tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                            ReservationTag>(
      world, std::span<tess::PathAgentState>(agents), routes, priorities,
      scratch, rank);

  EXPECT_EQ(agents[1].position, (tess::Coord3{2, 1, 0}));
  EXPECT_EQ(agents[1].phase, tess::PathAgentPhase::Unreachable);
  EXPECT_EQ(agents[1].blocked_retries, 0u);
  EXPECT_FALSE(agents[0].position == (tess::Coord3{2, 1, 0}));
}

// `clear_path_agent_goal` zeroes `goal`, so a goalless agent standing on the
// origin compares equal to it. Nothing may register an arrival for a journey
// that was never admitted: it would inflate `completed` and break the
// flow-accounting retention identity.
TEST(TessPibtMovement, GoallessAgentOnTheOriginNeverRegistersAnArrival) {
  World world;
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  fill_world(world, false);
  for (const auto c : {tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0},
                       tess::Coord3{2, 0, 0}, tess::Coord3{0, 1, 0}}) {
    world.field<PassableTag>(c) = true;
  }
  add_agent(world, agents, routes, {{1, 0, 0}, {0, 0, 0}});  // heads to origin
  add_agent(world, agents, routes, {{0, 0, 0}, {0, 0, 0}});  // sits on origin

  // clear_path_agent_goal zeroes `goal`, so this agent's goal compares
  // equal to the origin tile it happens to be standing on.
  tess::clear_path_agent_goal(agents[1]);
  ASSERT_EQ(agents[1].goal, (tess::Coord3{0, 0, 0}));

  tess::PibtPriorities priorities;
  priorities.elapsed = {100, 0};
  tess::JointMoveScratch scratch;
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal =
        agents[agent].has_goal ? agents[agent].goal : agents[agent].position;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };

  const auto stats =
      tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                          ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);

  // The goalless agent is neither moved nor counted as having arrived.
  EXPECT_EQ(agents[1].position, (tess::Coord3{0, 0, 0}));
  EXPECT_FALSE(agents[1].has_goal);
  EXPECT_EQ(stats.frame.arrived, 0u);
}

// `elapsed` is the caller's adaptive-priority knob and stays public; the
// decision order and the inheritance stack are rebuilt every pass and are
// not. `PrioritiesProbeControl` spells all three names in public, so a
// mistyped name cannot turn a negative assertion into one that no type could
// ever satisfy.
struct PrioritiesProbeControl {
  int elapsed = 0;
  int order = 0;
  int frames = 0;
};

template <typename T>
concept HasMemberElapsed = requires(T& value) { value.elapsed; };
template <typename T>
concept HasMemberOrder = requires(T& value) { value.order; };
template <typename T>
concept HasMemberFrames = requires(T& value) { value.frames; };

TEST(TessPibtMovement, OnlyElapsedIsPartOfThePrioritiesSurface) {
  using Priorities = tess::PibtPriorities;
  using Control = PrioritiesProbeControl;

  // The probes are satisfiable, so a mistyped name cannot make the negative
  // assertions below pass for the wrong reason.
  static_assert(HasMemberElapsed<Control>);
  static_assert(HasMemberOrder<Control>);
  static_assert(HasMemberFrames<Control>);

  static_assert(HasMemberElapsed<Priorities>);
  static_assert(!HasMemberOrder<Priorities>);
  static_assert(!HasMemberFrames<Priorities>);

  // `frames` was also the last public member typed with a `detail` struct,
  // so privatizing it is what closes the leak rather than promoting
  // `PibtFrame` and freezing an implementation layout.
  static_assert(
      requires(Priorities& priorities) { priorities.reserve(std::size_t{4}); });
  static_assert(std::is_default_constructible_v<Priorities>);
  static_assert(std::is_copy_constructible_v<Priorities>);
  static_assert(std::is_copy_assignable_v<Priorities>);
  static_assert(std::is_move_constructible_v<Priorities>);
  static_assert(std::is_move_assignable_v<Priorities>);
  static_assert(!std::is_aggregate_v<Priorities>);

  // Mixing a public `elapsed` with a private member costs standard layout,
  // so `offsetof` and the trait no longer apply. That is a real break beyond
  // the member names, and it is the price of keeping the caller's knob
  // public rather than hiding it behind an accessor pair. It only *changes*
  // anything where `std::vector` is itself standard-layout: under MSVC's STL
  // this assertion already held before the change, for that unrelated
  // reason.
  static_assert(!std::is_standard_layout_v<Priorities>);

  SUCCEED();
}

TEST(TessPibtMovement, RouteAttachmentRankingScoresLocalAttachment) {
  World world;
  fill_world(world, true);
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  // A straight 5-point route east: (2,2) .. (6,2).
  (void)add_agent(world, agents, routes,
                  {{2, 2, 0}, {3, 2, 0}, {4, 2, 0}, {5, 2, 0}, {6, 2, 0}});
  const tess::RouteAttachmentRanking rank{
      std::span<const tess::PathAgentState>{agents}, &routes};

  // On-route candidates score their remaining route length exactly.
  EXPECT_EQ(rank(0, {6, 2, 0}), 0u);
  EXPECT_EQ(rank(0, {4, 2, 0}), 2u);
  EXPECT_EQ(rank(0, {2, 2, 0}), 4u);
  // A one-tile hop onto the route adds its attachment distance.
  EXPECT_EQ(rank(0, {4, 3, 0}), 3u);
  // The best attachment wins: (5,3) attaches to (5,2) for 1+1=2, not to
  // (4,2) for 2+2=4 or (6,2) for 2+0=2 — ties resolve to the same score.
  EXPECT_EQ(rank(0, {5, 3, 0}), 2u);
  // Beyond the radius the candidate is detached and steered back by its
  // distance to the nearest route point, far above any attached score.
  const auto detached = rank(0, {10, 8, 0});
  EXPECT_GE(detached, tess::RouteAttachmentRanking::kDetachedBase);
  EXPECT_EQ(detached - tess::RouteAttachmentRanking::kDetachedBase, 10u);
  // Distance 2 is already detached under the default radius: such pairs
  // can sit on opposite sides of a one-tile wall, so admitting them
  // would reintroduce the lure one tile closer.
  EXPECT_GE(rank(0, {4, 4, 0}), tess::RouteAttachmentRanking::kDetachedBase);
  // The metric is three-axis: a level apart is apart, never colocated.
  EXPECT_GE(rank(0, {4, 2, 5}), tess::RouteAttachmentRanking::kDetachedBase);
  EXPECT_EQ(rank(0, {4, 2, 1}), 1u + 2u);

  // Agents without a usable route fall back to Manhattan toward the goal.
  tess::PathAgentState bare;
  bare.position = {1, 1, 0};
  bare.goal = {4, 1, 0};
  bare.has_goal = true;
  std::vector<tess::PathAgentState> bare_agents{bare};
  tess::PathAgentRoutes empty_routes;
  empty_routes.ensure_size(1);
  const tess::RouteAttachmentRanking fallback{
      std::span<const tess::PathAgentState>{bare_agents}, &empty_routes, 2};
  EXPECT_EQ(fallback(0, {3, 1, 0}), 1u);
}

// The measured wall-face lure, pinned as a regression: Manhattan
// attachment is wall-blind, so with an unbounded radius the far-side
// route points score a candidate pressed against the wall better than
// following the detour, and the agent parks at the wall face forever.
// The bounded radius admits only local attachments, so the same agent
// walks its detour and arrives. This is the mixed-colony stranding
// mechanism reduced to one agent.
TEST(TessPibtMovement, BoundedAttachmentArrivesWhereUnboundedParks) {
  for (const bool bounded : {false, true}) {
    World world;
    fill_world(world, true);
    // A wall at x=16 spanning y in [1, 31]; the doorway is at y=0.
    for (std::int64_t y = 1; y < 32; ++y) {
      world.field<PassableTag>({16, y, 0}) = false;
    }
    std::vector<tess::PathAgentState> agents;
    tess::PathAgentRoutes routes;
    // The detour route: west side up to the door, through, back down.
    std::vector<tess::Coord3> route;
    for (std::int64_t y = 16; y >= 0; --y) {
      route.push_back({2, y, 0});
    }
    for (std::int64_t x = 3; x <= 30; ++x) {
      route.push_back({x, 0, 0});
    }
    for (std::int64_t y = 1; y <= 16; ++y) {
      route.push_back({30, y, 0});
    }
    const auto goal = route.back();
    (void)add_agent(world, agents, routes, std::move(route));

    tess::PibtPriorities priorities;
    priorities.reserve(agents.size());
    tess::JointMoveScratch scratch;
    scratch.reserve(agents.size());
    const tess::RouteAttachmentRanking rank{
        std::span<const tess::PathAgentState>{agents}, &routes,
        bounded ? tess::RouteAttachmentRanking{}.attach_radius : 10'000u};
    for (int tick = 0; tick < 96 && agents[0].position != goal; ++tick) {
      (void)tess::advance_path_agents_with_pibt<World, Walker, OccupancyTag,
                                                ReservationTag>(
          world, std::span<tess::PathAgentState>(agents), routes, priorities,
          scratch, rank);
    }
    if (bounded) {
      EXPECT_EQ(agents[0].position, goal)
          << "bounded attachment must follow the detour";
    } else {
      EXPECT_NE(agents[0].position, goal)
          << "unbounded attachment is expected to park at the wall face; "
             "if this arrives, the lure regression no longer reproduces "
             "and the radius bound needs a new justification";
      EXPECT_LT(agents[0].position.x, 16)
          << "the lured agent should stay pressed against the west face";
    }
  }
}

}  // namespace

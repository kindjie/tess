// Experimental scoped-replan selection (issue #269 productization): the
// route-crossing query that keeps a periodic cost-field edit from
// replanning every agent. Fixtures cover the contract directly:
// crossing and non-crossing routes, the consumed-suffix boundary,
// skipped agent states, queue deduplication, ordering, and the
// first-hit stop.
#include <gtest/gtest.h>
#include <tess/experimental/path_agent_replan_selection.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace {

using tess::Coord3;
using tess::PathAgentPhase;
using tess::PathAgentReplanQueue;
using tess::PathAgentRoutes;
using tess::PathAgentState;

PathAgentState agent_at(int x, int y) {
  PathAgentState agent;
  agent.position = Coord3{x, y, 0};
  agent.has_goal = true;
  agent.phase = PathAgentPhase::Following;
  return agent;
}

std::vector<Coord3> line_route(int y, int x_begin, int x_end) {
  std::vector<Coord3> route;
  for (int x = x_begin; x <= x_end; ++x) {
    route.push_back(Coord3{x, y, 0});
  }
  return route;
}

struct Fixture {
  std::vector<PathAgentState> agents;
  PathAgentRoutes routes;
  PathAgentReplanQueue queue;

  std::size_t add(PathAgentState agent, std::vector<Coord3> route,
                  std::size_t path_index = 0) {
    agent.path_index = path_index;
    agents.push_back(agent);
    routes.routes.push_back(std::move(route));
    return agents.size() - 1;
  }

  template <typename Fn>
  std::size_t run(Fn&& fn) {
    return tess::experimental::request_replans_for_route_crossings(
        agents, routes, std::forward<Fn>(fn), queue);
  }
};

TEST(ReplanSelection, RequestsOnlyAgentsWhoseRemainingRouteCrosses) {
  Fixture f;
  const auto hit = f.add(agent_at(0, 0), line_route(0, 0, 5));
  (void)f.add(agent_at(0, 1), line_route(1, 0, 5));
  const auto count = f.run([](Coord3 c) { return c.y == 0 && c.x == 3; });
  EXPECT_EQ(count, 1U);
  // Compare optionals rather than dereferencing: gtest's ASSERT_TRUE is
  // not a flow guard the unchecked-optional-access check can follow.
  EXPECT_EQ(f.queue.front(), std::optional<std::size_t>{hit});
  f.queue.pop_front();
  EXPECT_TRUE(f.queue.empty());
}

TEST(ReplanSelection, ConsumedPrefixDoesNotCount) {
  Fixture f;
  // The increase sits behind the agent: path_index is already past it.
  (void)f.add(agent_at(3, 0), line_route(0, 0, 5), 3U);
  EXPECT_EQ(f.run([](Coord3 c) { return c.x == 1; }), 0U);
  // The occupied tile (route[path_index]) does NOT count: its cost was
  // paid on entering it, so a price rise there cannot change the cost
  // of the route that remains.
  EXPECT_EQ(f.run([](Coord3 c) { return c.x == 3; }), 0U);
  // The next step does count.
  EXPECT_EQ(f.run([](Coord3 c) { return c.x == 4; }), 1U);
}

TEST(ReplanSelection, SkipsGoallessUnreachableAndRouteless) {
  Fixture f;
  auto arrived = agent_at(5, 0);
  arrived.has_goal = false;
  (void)f.add(arrived, line_route(0, 0, 5));
  auto unreachable = agent_at(0, 1);
  unreachable.phase = PathAgentPhase::Unreachable;
  (void)f.add(unreachable, line_route(1, 0, 5));
  (void)f.add(agent_at(0, 2), {});  // empty route
  auto consumed = agent_at(5, 3);
  (void)f.add(consumed, line_route(3, 0, 5), 6U);  // fully consumed
  EXPECT_EQ(f.run([](Coord3) { return true; }), 0U);
}

TEST(ReplanSelection, MissingRouteEntryContributesNothing) {
  Fixture f;
  (void)f.add(agent_at(0, 0), line_route(0, 0, 2));
  f.agents.push_back(agent_at(0, 1));  // no matching routes entry
  EXPECT_EQ(f.run([](Coord3) { return true; }), 1U);
}

TEST(ReplanSelection, DeduplicatesThroughTheQueue) {
  Fixture f;
  const auto i = f.add(agent_at(0, 0), line_route(0, 0, 5));
  ASSERT_TRUE(f.queue.request(i, f.agents[i]));
  EXPECT_EQ(f.run([](Coord3) { return true; }), 0U);
}

TEST(ReplanSelection, StopsAtFirstCrossingPerAgent) {
  Fixture f;
  (void)f.add(agent_at(0, 0), line_route(0, 0, 5));
  std::size_t consultations = 0;
  const auto count = f.run([&consultations](Coord3 c) {
    ++consultations;
    return c.x >= 1;
  });
  EXPECT_EQ(count, 1U);
  // The scan begins at the next step, so x=1 is the first consultation
  // and it hits; the occupied tile at x=0 is never consulted.
  EXPECT_EQ(consultations, 1U);
}

TEST(ReplanSelection, AscendingAgentIndexOrder) {
  Fixture f;
  const auto a = f.add(agent_at(0, 0), line_route(0, 0, 2));
  const auto b = f.add(agent_at(0, 1), line_route(1, 0, 2));
  EXPECT_EQ(f.run([](Coord3) { return true; }), 2U);
  EXPECT_EQ(f.queue.front(), std::optional<std::size_t>{a});
  f.queue.pop_front();
  EXPECT_EQ(f.queue.front(), std::optional<std::size_t>{b});
}

TEST(ReplanSelection, PendingAgentIsNeitherRequeuedNorRescanned) {
  Fixture f;
  const auto a = f.add(agent_at(0, 0), line_route(0, 0, 5));
  const auto b = f.add(agent_at(0, 1), line_route(1, 0, 5));
  // A is already waiting; B is not. Both routes cross the increase.
  ASSERT_TRUE(f.queue.request(a, f.agents[a]));
  ASSERT_TRUE(f.queue.contains(a));
  ASSERT_FALSE(f.queue.contains(b));

  std::vector<Coord3> consulted;
  const auto count = f.run([&consulted](Coord3 c) {
    consulted.push_back(c);
    return c.x == 3;
  });

  // Semantics: the return counts only agents newly queued by this call,
  // the queue holds both exactly once, and FIFO order is preserved.
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(f.queue.pending(), 2U);
  EXPECT_EQ(f.queue.front(), std::optional<std::size_t>{a});
  f.queue.pop_front();
  EXPECT_EQ(f.queue.front(), std::optional<std::size_t>{b});
  // The optimisation itself: A's route was never consulted. This fails
  // if the skip is removed, and the assertions above still hold either
  // way, so they are the semantic proof and this is the regression pin.
  EXPECT_TRUE(std::none_of(consulted.begin(), consulted.end(),
                           [](Coord3 c) { return c.y == 0; }));
}

TEST(ReplanSelection, DrainedAgentsAreScannedAndQueuedAgain) {
  Fixture f;
  std::vector<std::size_t> ids;
  for (int lane = 0; lane < 4; ++lane) {
    ids.push_back(f.add(agent_at(0, lane), line_route(lane, 0, 5)));
  }
  EXPECT_EQ(f.run([](Coord3 c) { return c.x == 3; }), 4U);
  // Drain two. Membership is the PENDING set, not a record of having
  // ever been requested, so the drained pair must be scanned and queued
  // again by the next selection. A sticky bit would silently drop them.
  f.queue.pop_front();
  f.queue.pop_front();
  EXPECT_FALSE(f.queue.contains(ids[0]));
  EXPECT_FALSE(f.queue.contains(ids[1]));
  EXPECT_EQ(f.run([](Coord3 c) { return c.x == 3; }), 2U);
  EXPECT_EQ(f.queue.pending(), 4U);
}

TEST(ReplanSelection, MembershipOnUnreservedQueueStaysInBounds) {
  // `queued_` is sized lazily, so an index never requested must report
  // false rather than read past the end.
  PathAgentReplanQueue fresh;
  EXPECT_FALSE(fresh.contains(0U));
  EXPECT_FALSE(fresh.contains(4096U));
}

TEST(ReplanSelection, QueueMembershipTracksRequestAndDrain) {
  Fixture f;
  const auto a = f.add(agent_at(0, 0), line_route(0, 0, 3));
  EXPECT_FALSE(f.queue.contains(a));
  EXPECT_TRUE(f.queue.request(a, f.agents[a]));
  EXPECT_TRUE(f.queue.contains(a));
  // A second request for a pending agent is refused, not duplicated.
  EXPECT_FALSE(f.queue.request(a, f.agents[a]));
  EXPECT_EQ(f.queue.pending(), 1U);
  f.queue.pop_front();
  EXPECT_FALSE(f.queue.contains(a));
  // Out of range never reports membership.
  EXPECT_FALSE(f.queue.contains(9999U));
  EXPECT_TRUE(f.queue.request(a, f.agents[a]));
  f.queue.clear();
  EXPECT_FALSE(f.queue.contains(a));
}

}  // namespace

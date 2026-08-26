// Experimental scoped-replan selection (issue #269 productization): the
// route-crossing query that keeps a periodic cost-field edit from
// replanning every agent. Fixtures cover the contract directly:
// crossing and non-crossing routes, the consumed-suffix boundary,
// skipped agent states, queue deduplication, ordering, and the
// first-hit stop.
#include <gtest/gtest.h>
#include <tess/experimental/path_agent_replan_selection.h>

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
  ASSERT_TRUE(f.queue.front().has_value());
  EXPECT_EQ(*f.queue.front(), hit);
  f.queue.pop_front();
  EXPECT_TRUE(f.queue.empty());
}

TEST(ReplanSelection, ConsumedPrefixDoesNotCount) {
  Fixture f;
  // The increase sits behind the agent: path_index is already past it.
  (void)f.add(agent_at(3, 0), line_route(0, 0, 5), 3U);
  EXPECT_EQ(f.run([](Coord3 c) { return c.x == 1; }), 0U);
  // The current tile (route[path_index]) DOES count -- the documented
  // contract detail an eventual stable promotion must settle.
  EXPECT_EQ(f.run([](Coord3 c) { return c.x == 3; }), 1U);
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
  // Consulted for x=0 (miss) and x=1 (hit), then stopped.
  EXPECT_EQ(consultations, 2U);
}

TEST(ReplanSelection, AscendingAgentIndexOrder) {
  Fixture f;
  const auto a = f.add(agent_at(0, 0), line_route(0, 0, 2));
  const auto b = f.add(agent_at(0, 1), line_route(1, 0, 2));
  EXPECT_EQ(f.run([](Coord3) { return true; }), 2U);
  ASSERT_TRUE(f.queue.front().has_value());
  EXPECT_EQ(*f.queue.front(), a);
  f.queue.pop_front();
  ASSERT_TRUE(f.queue.front().has_value());
  EXPECT_EQ(*f.queue.front(), b);
}

}  // namespace

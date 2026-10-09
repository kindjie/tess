#include <gtest/gtest.h>
#include <tess/navigation/query.h>

#include <memory>
#include <vector>

namespace {
using namespace tess;

TEST(Navigation, DirectGraphEdgeRejectsInvalidLookup) {
  NavigationGraph invalid(2, {{0, 1, 7, 1}, {0, 2, 8, 1}});
  ASSERT_FALSE(invalid.valid());
  // Fail deterministically on the invalid graph before probing absent storage.
  ASSERT_EQ(invalid.edge(0, 0, false).availability,
            NavigationAvailability::Invalid);
  NavigationGraph graph(2, {{0, 1, 7, 1}});
  for (const bool reverse : {false, true}) {
    EXPECT_EQ(graph.edge(2, 0, reverse).availability,
              NavigationAvailability::Invalid);
    EXPECT_EQ(graph.edge(0, 99, reverse).availability,
              NavigationAvailability::Invalid);
    EXPECT_EQ(graph.edge(1, 99, reverse).availability,
              NavigationAvailability::Invalid);
  }
  EXPECT_EQ(graph.edge(0, 0, false).id, 7u);
  EXPECT_EQ(graph.edge(1, 0, true).id, 7u);
  NavigationGraph empty(0, {});
  EXPECT_EQ(empty.edge(0, 0, false).availability,
            NavigationAvailability::Invalid);
}

TEST(Navigation, WholeJourneyAndParallelEdgeIdentity) {
  auto street = std::make_shared<NavigationGraph>(
      3, std::vector<NavigationLocalEdge>{{0, 1, 11, 10}, {0, 2, 12, 20}});
  auto room = std::make_shared<NavigationGraph>(
      3, std::vector<NavigationLocalEdge>{{0, 2, 21, 60}, {1, 2, 22, 10}});
  auto snapshot = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{street, room},
      std::vector<NavigationConnection>{
          {street->location(1), room->location(0), 31, 0},
          {street->location(2), room->location(1), 32, 0},
          {street->location(2), room->location(1), 33, 50}});
  ASSERT_TRUE(snapshot->valid());
  NavigationQuery query(snapshot, street->location(0), room->location(2));
  while (query.state() == AsyncResultState::Pending) {
    auto step = query.advance({1});
    EXPECT_EQ(step.items_done, 1u);
  }
  ASSERT_EQ(query.state(), AsyncResultState::Ready);
  ASSERT_NE(query.result(), nullptr);
  EXPECT_EQ(query.result()->outcome, NavigationOutcome::Found);
  EXPECT_EQ(query.result()->cost, 30u);
  ASSERT_EQ(query.result()->route.size(), 3u);
  EXPECT_EQ(query.result()->route[1].id, 32u);
  EXPECT_TRUE(query.result()->route[1].connection);
  NavigationQuery guidance(snapshot, room->location(2));
  while (guidance.state() == AsyncResultState::Pending) guidance.advance({1});
  ASSERT_NE(guidance.result(), nullptr);
  auto value = guidance.result()->guidance_at(street->location(0));
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->cost, 30u);
}

TEST(Navigation, ZeroCyclesAndRetainedSnapshot) {
  auto graph = std::make_shared<NavigationGraph>(
      3, std::vector<NavigationLocalEdge>{
             {0, 1, 1, 0}, {1, 0, 2, 0}, {1, 2, 3, 0}});
  auto snapshot = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph});
  NavigationQuery query(snapshot, graph->location(0), graph->location(2));
  while (query.state() == AsyncResultState::Pending) query.advance({1});
  ASSERT_NE(query.result(), nullptr);
  EXPECT_EQ(query.result()->cost, 0u);
  EXPECT_EQ(query.result()->route.size(), 2u);
  auto replacement = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph});
  EXPECT_FALSE(query.result()->current_for(replacement));
  EXPECT_TRUE(query.result()->current_for(snapshot));
}

TEST(Navigation, RefusalCancellationAndUnknownAreDistinct) {
  auto graph = std::make_shared<NavigationGraph>(
      3, std::vector<NavigationLocalEdge>{{0, 1, 1, 1}, {1, 2, 2, 1}});
  auto snapshot = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph});
  NavigationQuery query(snapshot, graph->location(0), graph->location(2),
                        NavigationLimits{1, 4, 4});
  while (query.state() == AsyncResultState::Pending) query.advance({1});
  ASSERT_NE(query.result(), nullptr);
  EXPECT_EQ(query.result()->outcome, NavigationOutcome::CapacityExceeded);
  EXPECT_TRUE(query.result()->route.empty());
  NavigationQuery cancelled(snapshot, graph->location(0), graph->location(2));
  cancelled.cancel();
  EXPECT_EQ(cancelled.state(), AsyncResultState::Cancelled);
  EXPECT_EQ(cancelled.result(), nullptr);
}
}  // namespace

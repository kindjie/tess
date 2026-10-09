#include <gtest/gtest.h>
#include <tess/navigation/grid.h>
#include <tess/navigation/query.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace {
using namespace tess;
constexpr auto kReserved = std::numeric_limits<std::uint64_t>::max();
constexpr NavigationLimits kLimits{64, 512, 64};

// The oracle consumes authored edges directly, never adapter enumeration or
// query labels. Floyd-Warshall deliberately differs from production Dijkstra.
struct Distance {
  bool reachable = false;
  std::uint64_t cost = 0;
};
using Matrix = std::vector<std::vector<Distance>>;
auto reference_distances(std::size_t nodes,
                         const std::vector<NavigationLocalEdge>& edges)
    -> Matrix {
  Matrix result(nodes, std::vector<Distance>(nodes));
  for (std::size_t i = 0; i < nodes; ++i) result[i][i] = {true, 0};
  for (const auto& edge : edges) {
    if (edge.availability != NavigationAvailability::Legal) continue;
    auto& distance = result[edge.from][edge.to];
    if (!distance.reachable || edge.cost < distance.cost)
      distance = {true, edge.cost};
  }
  for (std::size_t k = 0; k < nodes; ++k) {
    for (std::size_t i = 0; i < nodes; ++i) {
      for (std::size_t j = 0; j < nodes; ++j) {
        const auto a = result[i][k];
        const auto b = result[k][j];
        if (!a.reachable || !b.reachable || b.cost >= kReserved - a.cost)
          continue;
        const auto cost = a.cost + b.cost;
        auto& distance = result[i][j];
        if (!distance.reachable || cost < distance.cost)
          distance = {true, cost};
      }
    }
  }
  return result;
}

auto snapshot_of(const std::shared_ptr<const NavigationDomain>& graph)
    -> std::shared_ptr<const NavigationSnapshot> {
  return std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph});
}

auto finish_query(NavigationQuery& query, std::uint32_t budget = 1)
    -> std::shared_ptr<const NavigationResult> {
  for (std::size_t calls = 0;
       query.state() == AsyncResultState::Pending && calls < 100000; ++calls) {
    const auto step = query.advance({budget});
    EXPECT_LE(step.items_done, budget);
    EXPECT_GT(step.items_done, 0u);
  }
  EXPECT_EQ(query.state(), AsyncResultState::Ready);
  return query.retained_result();
}

void check_route(const NavigationResult& result,
                 const std::vector<NavigationLocalEdge>& edges,
                 NavigationLocation start, NavigationLocation goal) {
  auto cursor = start;
  std::uint64_t cost = 0;
  for (const auto& step : result.route) {
    EXPECT_EQ(step.from, cursor);
    EXPECT_FALSE(step.connection);
    const auto original =
        std::find_if(edges.begin(), edges.end(),
                     [&](const auto& edge) { return edge.id == step.id; });
    ASSERT_NE(original, edges.end());
    EXPECT_EQ(original->from, step.from.node);
    EXPECT_EQ(original->to, step.to.node);
    EXPECT_EQ(original->cost, step.cost);
    EXPECT_EQ(original->key, step.key);
    EXPECT_EQ(original->availability, NavigationAvailability::Legal);
    ASSERT_LT(step.cost, kReserved - cost);
    cost += step.cost;
    cursor = step.to;
  }
  EXPECT_EQ(cursor, goal);
  EXPECT_EQ(cost, result.cost);
}

TEST(NavigationOracle, DirectedRandomGraphsMatchIndependentAllPairs) {
  constexpr std::size_t nodes = 7;
  std::uint32_t random = 0x159b4u;
  auto next = [&] {
    random = random * 1664525u + 1013904223u;
    return random;
  };
  for (std::size_t trial = 0; trial < 12; ++trial) {
    std::vector<NavigationLocalEdge> edges;
    for (std::size_t id = 0; id < 45; ++id) {
      edges.push_back({next() % nodes, next() % nodes, id, next() % 9, id + 100,
                       next() % 5 == 0 ? NavigationAvailability::Blocked
                                       : NavigationAvailability::Legal});
    }
    const auto oracle = reference_distances(nodes, edges);
    auto graph = std::make_shared<NavigationGraph>(nodes, edges);
    auto snapshot = snapshot_of(graph);
    for (std::size_t from = 0; from < nodes; ++from) {
      for (std::size_t to = 0; to < nodes; ++to) {
        SCOPED_TRACE(::testing::Message() << trial << ':' << from << ':' << to);
        NavigationQuery query(snapshot, graph->location(from),
                              graph->location(to), kLimits);
        const auto result = finish_query(query, trial % 2 == 0 ? 1 : 3);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->proof, NavigationProof::ExactSnapshot);
        const auto expected = oracle[from][to];
        EXPECT_EQ(result->outcome, expected.reachable
                                       ? NavigationOutcome::Found
                                       : NavigationOutcome::NoRoute);
        if (expected.reachable) {
          EXPECT_EQ(result->cost, expected.cost);
          check_route(*result, edges, graph->location(from),
                      graph->location(to));
        } else {
          EXPECT_TRUE(result->route.empty());
        }
        const auto sync = navigation_route(snapshot, graph->location(from),
                                           graph->location(to), kLimits);
        ASSERT_NE(sync, nullptr);
        EXPECT_EQ(sync->outcome, result->outcome);
        EXPECT_EQ(sync->cost, result->cost);
        EXPECT_EQ(sync->route.size(), result->route.size());
        for (std::size_t i = 0; i < result->route.size(); ++i)
          EXPECT_EQ(sync->route[i].id, result->route[i].id);
      }
    }
    NavigationQuery reverse(snapshot, graph->location(6), kLimits);
    const auto product = finish_query(reverse);
    ASSERT_NE(product, nullptr);
    for (std::size_t from = 0; from < nodes; ++from) {
      auto at = graph->location(from);
      const auto* label = product->guidance_at(at);
      if (!oracle[from][6].reachable) {
        EXPECT_EQ(label, nullptr);
        continue;
      }
      ASSERT_NE(label, nullptr);
      EXPECT_EQ(label->cost, oracle[from][6].cost);
      std::size_t steps = 0;
      while (label->has_next && steps < nodes) {
        EXPECT_EQ(label->next.from, at);
        at = label->next.to;
        label = product->guidance_at(at);
        ASSERT_NE(label, nullptr);
        ++steps;
      }
      EXPECT_LT(steps, nodes);
      EXPECT_EQ(at, graph->location(6));
    }
  }
}

struct OracleOpen {};
struct OracleWeight {};
using OracleHallShape = Shape<Extent3{4, 1, 1}, Extent3{2, 1, 1}>;
using OracleFloorShape = Shape<Extent3{2, 2, 1}, Extent3{2, 2, 1}>;
using OracleHall =
    AlwaysResidentWorld<OracleHallShape, FieldSchema<Field<OracleOpen, bool>>>;
using OracleFloor =
    AlwaysResidentWorld<OracleFloorShape,
                        FieldSchema<Field<OracleWeight, std::uint32_t>>>;
using OracleHallMovement = movement::UnitCostFieldMovement<OracleOpen>;
using OracleFloorMovement =
    movement::MovementClass<movement::NotZero<OracleWeight>,
                            movement::FieldCost<OracleWeight>>;

TEST(NavigationOracle, MixedDomainsMatchAllPairsAndCompleteGuidance) {
  auto hall_world = std::make_shared<OracleHall>();
  hall_world->fill_field<OracleOpen>(true);
  auto floor_world = std::make_shared<OracleFloor>();
  floor_world->field<OracleWeight>({0, 0, 0}) = 1;
  floor_world->field<OracleWeight>({1, 0, 0}) = 4;
  floor_world->field<OracleWeight>({0, 1, 0}) = 2;
  floor_world->field<OracleWeight>({1, 1, 0}) = 3;
  auto hall = std::make_shared<NavigationGrid<OracleHall, OracleHallMovement>>(
      std::move(hall_world), 2);
  auto floor =
      std::make_shared<NavigationGrid<OracleFloor, OracleFloorMovement>>(
          std::move(floor_world), 3);
  auto road = std::make_shared<NavigationGraph>(
      2, std::vector<NavigationLocalEdge>{{0, 1, 90, 50, 900},
                                          {1, 0, 91, 7, 901}});
  const std::vector<NavigationLocation> locations{
      road->location(0),
      road->location(1),
      hall->coordinate_location({0, 0, 0}),
      hall->coordinate_location({1, 0, 0}),
      hall->coordinate_location({2, 0, 0}),
      hall->coordinate_location({3, 0, 0}),
      floor->coordinate_location({0, 0, 0}),
      floor->coordinate_location({1, 0, 0}),
      floor->coordinate_location({0, 1, 0}),
      floor->coordinate_location({1, 1, 0})};
  const std::vector<NavigationConnection> seams{
      {locations[0], locations[2], 100, 0, 1000},
      {locations[2], locations[0], 101, 0, 1001},
      {locations[5], locations[6], 102, 8, 1002},
      {locations[5], locations[6], 103, 1, 1003},
      {locations[5], locations[6], 104, 0, 1004,
       NavigationAvailability::Blocked},
      {locations[6], locations[5], 105, 0, 1005},
      {locations[9], locations[1], 106, 0, 1006},
      {locations[1], locations[3], 107, 2, 1007}};
  auto snapshot = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{floor, road, hall},
      seams);
  // Independently authored adjacency in fixture indices, not packed node IDs
  // or production enumeration. Floor edges charge the destination weight * 3;
  // hall edges cost 2. The cheapest road-to-road journey leaves and re-enters
  // that domain; zero-cost cycles and blocked/parallel seams are deliberate.
  const std::vector<NavigationLocalEdge> reference{
      {0, 1, 90, 50, 900},
      {1, 0, 91, 7, 901},
      {2, 3, 0, 2},
      {3, 2, 0, 2},
      {3, 4, 0, 2},
      {4, 3, 0, 2},
      {4, 5, 0, 2},
      {5, 4, 0, 2},
      {6, 7, 0, 12},
      {7, 6, 0, 3},
      {6, 8, 0, 6},
      {8, 6, 0, 3},
      {7, 9, 0, 9},
      {9, 7, 0, 12},
      {8, 9, 0, 9},
      {9, 8, 0, 6},
      {0, 2, 100, 0, 1000},
      {2, 0, 101, 0, 1001},
      {5, 6, 102, 8, 1002},
      {5, 6, 103, 1, 1003},
      {5, 6, 104, 0, 1004, NavigationAvailability::Blocked},
      {6, 5, 105, 0, 1005},
      {9, 1, 106, 0, 1006},
      {1, 3, 107, 2, 1007}};
  const auto oracle = reference_distances(locations.size(), reference);
  ASSERT_EQ(oracle[0][1].cost, 22u);
  ASSERT_EQ(oracle[1][0].cost, 4u);
  auto check_step = [&](const NavigationTransition& step) {
    const auto from = std::find(locations.begin(), locations.end(), step.from);
    const auto to = std::find(locations.begin(), locations.end(), step.to);
    ASSERT_NE(from, locations.end());
    ASSERT_NE(to, locations.end());
    const auto from_index = static_cast<std::size_t>(from - locations.begin());
    const auto to_index = static_cast<std::size_t>(to - locations.begin());
    const auto edge = std::find_if(
        reference.begin(), reference.end(), [&](const auto& candidate) {
          return candidate.from == from_index && candidate.to == to_index &&
                 candidate.cost == step.cost &&
                 candidate.availability == NavigationAvailability::Legal &&
                 (!(step.connection || step.from.domain == road->identity()) ||
                  (candidate.id == step.id && candidate.key == step.key));
        });
    EXPECT_NE(edge, reference.end());
    EXPECT_EQ(step.connection, step.from.domain != step.to.domain);
  };
  for (std::size_t goal = 0; goal < locations.size(); ++goal) {
    NavigationQuery shared(snapshot, locations[goal], kLimits);
    const auto guidance = finish_query(shared);
    ASSERT_NE(guidance, nullptr);
    ASSERT_EQ(guidance->outcome, NavigationOutcome::Found);
    EXPECT_EQ(guidance->proof, NavigationProof::ExactSnapshot);
    for (std::size_t start = 0; start < locations.size(); ++start) {
      SCOPED_TRACE(::testing::Message() << start << " -> " << goal);
      ASSERT_TRUE(oracle[start][goal].reachable);
      NavigationQuery query(snapshot, locations[start], locations[goal],
                            kLimits);
      const auto route = finish_query(query, 1);
      ASSERT_NE(route, nullptr);
      ASSERT_EQ(route->outcome, NavigationOutcome::Found);
      EXPECT_EQ(route->proof, NavigationProof::ExactSnapshot);
      EXPECT_EQ(route->cost, oracle[start][goal].cost);
      auto at = locations[start];
      std::uint64_t cost = 0;
      for (const auto& step : route->route) {
        EXPECT_EQ(step.from, at);
        check_step(step);
        cost += step.cost;
        at = step.to;
      }
      EXPECT_EQ(at, locations[goal]);
      EXPECT_EQ(cost, oracle[start][goal].cost);
      const auto sync = navigation_route(snapshot, locations[start],
                                         locations[goal], kLimits);
      ASSERT_NE(sync, nullptr);
      EXPECT_EQ(sync->cost, route->cost);
      ASSERT_EQ(sync->route.size(), route->route.size());
      for (std::size_t i = 0; i < route->route.size(); ++i) {
        EXPECT_EQ(sync->route[i].from, route->route[i].from);
        EXPECT_EQ(sync->route[i].to, route->route[i].to);
        EXPECT_EQ(sync->route[i].id, route->route[i].id);
      }
      at = locations[start];
      cost = 0;
      std::vector<NavigationLocation> visited;
      while (at != locations[goal] && visited.size() < locations.size()) {
        ASSERT_EQ(std::find(visited.begin(), visited.end(), at), visited.end());
        visited.push_back(at);
        const auto* label = guidance->guidance_at(at);
        ASSERT_NE(label, nullptr);
        ASSERT_TRUE(label->has_next);
        EXPECT_EQ(label->next.from, at);
        check_step(label->next);
        const auto* next = guidance->guidance_at(label->next.to);
        ASSERT_NE(next, nullptr);
        EXPECT_EQ(label->cost, label->next.cost + next->cost);
        cost += label->next.cost;
        at = label->next.to;
      }
      EXPECT_EQ(at, locations[goal]);
      EXPECT_EQ(cost, oracle[start][goal].cost);
      const auto* origin = guidance->guidance_at(locations[start]);
      ASSERT_NE(origin, nullptr);
      EXPECT_EQ(origin->cost, oracle[start][goal].cost);
      const auto* terminal = guidance->guidance_at(locations[goal]);
      ASSERT_NE(terminal, nullptr);
      EXPECT_FALSE(terminal->has_next);
      EXPECT_EQ(terminal->cost, 0u);
    }
  }
}

TEST(NavigationOracle, ParallelZeroEdgesPreserveChosenIdentity) {
  const std::vector<NavigationLocalEdge> edges{
      {0, 1, 11, 8, 101},
      {0, 1, 12, 0, 102},
      {0, 1, 13, 0, 103, NavigationAvailability::Blocked},
      {1, 0, 14, 0},
      {1, 2, 15, 0},
      {2, 1, 16, 0}};
  auto graph = std::make_shared<NavigationGraph>(3, edges);
  auto result = navigation_route(snapshot_of(graph), graph->location(0),
                                 graph->location(2), kLimits);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(result->outcome, NavigationOutcome::Found);
  ASSERT_EQ(result->route.size(), 2u);
  EXPECT_EQ(result->route[0].id, 12u);
  EXPECT_EQ(result->route[0].key, 102u);
  check_route(*result, edges, graph->location(0), graph->location(2));
}

TEST(NavigationOracle, MultiGoalGuidanceIncludesBoundaryCosts) {
  const std::vector<NavigationLocalEdge> edges{{0, 1, 1, 10}, {0, 2, 2, 20},
                                               {1, 3, 3, 0},  {2, 4, 4, 0},
                                               {3, 1, 5, 0},  {4, 2, 6, 0}};
  auto graph = std::make_shared<NavigationGraph>(5, edges);
  NavigationQuery query(snapshot_of(graph),
                        std::vector<NavigationGoal>{{graph->location(3), 60},
                                                    {graph->location(4), 10},
                                                    {graph->location(4), 90}},
                        kLimits);
  const auto result = finish_query(query);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(result->outcome, NavigationOutcome::Found);
  const auto oracle = reference_distances(5, edges);
  for (std::size_t i = 0; i < 5; ++i) {
    const auto* label = result->guidance_at(graph->location(i));
    ASSERT_NE(label, nullptr);
    auto expected = kReserved;
    if (oracle[i][3].reachable) expected = oracle[i][3].cost + 60;
    if (oracle[i][4].reachable)
      expected = std::min(expected, oracle[i][4].cost + 10);
    EXPECT_EQ(label->cost, expected);
  }
  const auto* origin = result->guidance_at(graph->location(0));
  ASSERT_NE(origin, nullptr);
  EXPECT_EQ(origin->cost, 30u);
  EXPECT_EQ(origin->next.id, 2u);
}

TEST(NavigationOracle, SummedOverflowDoesNotHideRepresentableAlternative) {
  auto graph = std::make_shared<NavigationGraph>(
      4,
      std::vector<NavigationLocalEdge>{
          {0, 1, 1, 1}, {1, 3, 2, kReserved - 1}, {0, 2, 3, 9}, {2, 3, 4, 2}});
  auto result = navigation_route(snapshot_of(graph), graph->location(0),
                                 graph->location(3), kLimits);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::Found);
  EXPECT_EQ(result->cost, 11u);
  auto exact_limit = std::make_shared<NavigationGraph>(
      2, std::vector<NavigationLocalEdge>{{0, 1, 1, kReserved - 1}});
  result = navigation_route(snapshot_of(exact_limit), exact_limit->location(0),
                            exact_limit->location(1), kLimits);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::Found);
  EXPECT_EQ(result->cost, kReserved - 1);
  auto overflow = std::make_shared<NavigationGraph>(
      3,
      std::vector<NavigationLocalEdge>{{0, 1, 1, 1}, {1, 2, 2, kReserved - 1}});
  result = navigation_route(snapshot_of(overflow), overflow->location(0),
                            overflow->location(2), kLimits);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::Overflow);
  EXPECT_TRUE(result->route.empty());
  EXPECT_TRUE(result->guidance.empty());
}

TEST(NavigationOracle, GuidanceIgnoresDominatedAndLaterResolvedOverflow) {
  for (bool late : {false, true}) {
    SCOPED_TRACE(late);
    auto graph = std::make_shared<NavigationGraph>(
        4, late ? std::vector<NavigationLocalEdge>{{0, 3, 1, 1},
                                                   {1, 0, 2, kReserved - 1},
                                                   {2, 3, 3, 5},
                                                   {1, 2, 4, 4}}
                : std::vector<NavigationLocalEdge>{
                      {0, 3, 1, 1}, {0, 0, 2, kReserved - 1}, {1, 0, 3, 2}});
    NavigationQuery query(snapshot_of(graph), graph->location(3), kLimits);
    auto result = finish_query(query);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->outcome, NavigationOutcome::Found);
    if (result->outcome != NavigationOutcome::Found) continue;
    ASSERT_NE(result->guidance_at(graph->location(0)), nullptr);
    ASSERT_NE(result->guidance_at(graph->location(1)), nullptr);
    EXPECT_EQ(result->guidance_at(graph->location(0))->cost, 1u);
    EXPECT_EQ(result->guidance_at(graph->location(1))->cost, late ? 9u : 3u);
  }
}

TEST(NavigationOracle, GuidanceRefusesUnresolvedOverflowWithoutPartialOutput) {
  auto graph = std::make_shared<NavigationGraph>(
      3,
      std::vector<NavigationLocalEdge>{{0, 1, 1, kReserved - 1}, {1, 2, 2, 1}});
  NavigationQuery query(snapshot_of(graph), graph->location(2), kLimits);
  auto result = finish_query(query);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::Overflow);
  EXPECT_TRUE(result->guidance.empty());
}

TEST(NavigationOracle, OverflowEndpointsRespectDiscoveredCapacity) {
  auto graph = std::make_shared<NavigationGraph>(
      4,
      std::vector<NavigationLocalEdge>{
          {0, 3, 1, 1}, {1, 0, 2, kReserved - 1}, {2, 3, 3, 5}, {1, 2, 4, 4}});
  NavigationQuery query(snapshot_of(graph), graph->location(3),
                        NavigationLimits{3, 16, 8});
  auto result = finish_query(query);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::CapacityExceeded);
  EXPECT_TRUE(result->guidance.empty());
}

TEST(NavigationOracle, PublicationNeverReclaimsAnOccupiedOutputSlot) {
  auto graph =
      std::make_shared<NavigationGraph>(1, std::vector<NavigationLocalEdge>{});
  NavigationQuery query(snapshot_of(graph), graph->location(0), kLimits);
  auto result = finish_query(query);
  ASSERT_EQ(result->outcome, NavigationOutcome::Found);
  int destroyed = 0;
  std::shared_ptr<const NavigationResult> output(
      new NavigationResult, [&destroyed](const NavigationResult* old) {
        ++destroyed;
        delete old;
      });
  const auto old = output.get();
  const auto step = query({0}, output);
  EXPECT_EQ(step.state, AsyncStepState::Failed);
  EXPECT_EQ(step.items_done, 0u);
  EXPECT_EQ(output.get(), old);
  EXPECT_EQ(destroyed, 0);
  output.reset();
  EXPECT_EQ(destroyed, 1);
  EXPECT_EQ(query({0}, output).state, AsyncStepState::Ready);
  EXPECT_EQ(output, result);
  EXPECT_EQ(query({0}, output).state, AsyncStepState::Ready);
  for (bool null_alias : {false, true}) {
    auto foreign_owner = std::shared_ptr<const NavigationResult>(
        new NavigationResult, [&destroyed](const NavigationResult* old_value) {
          ++destroyed;
          delete old_value;
        });
    output = {foreign_owner, null_alias ? nullptr : result.get()};
    foreign_owner.reset();
    const auto before = destroyed;
    EXPECT_EQ(query({0}, output).state, AsyncStepState::Failed);
    EXPECT_EQ(destroyed, before);
    output.reset();
    EXPECT_EQ(destroyed, before + 1);
  }
}

TEST(NavigationOracle, ReservedCostInvalidatesGraphConnectionAndGoal) {
  auto invalid = std::make_shared<NavigationGraph>(
      2, std::vector<NavigationLocalEdge>{{0, 1, 1, kReserved}});
  auto result = navigation_route(snapshot_of(invalid), invalid->location(0),
                                 invalid->location(1), kLimits);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::InvalidInput);
  auto graph =
      std::make_shared<NavigationGraph>(2, std::vector<NavigationLocalEdge>{});
  auto snapshot = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph},
      std::vector<NavigationConnection>{
          {graph->location(0), graph->location(1), 1, kReserved}});
  EXPECT_FALSE(snapshot->valid());
  NavigationQuery query(
      snapshot_of(graph),
      std::vector<NavigationGoal>{{graph->location(1), kReserved}}, kLimits);
  result = finish_query(query);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::InvalidInput);
  EXPECT_TRUE(result->guidance.empty());
}

// Mutable counters observe calls, while all spatial input remains immutable.
class CountingDomain final : public NavigationDomain {
 public:
  CountingDomain(std::size_t nodes, std::vector<NavigationLocalEdge> edges)
      : edges_(std::move(edges)), forward_(nodes), reverse_(nodes) {
    for (std::size_t i = 0; i < edges_.size(); ++i) {
      forward_[edges_[i].from].push_back(i);
      reverse_[edges_[i].to].push_back(i);
    }
  }
  auto contains(std::uint64_t node) const noexcept -> bool override {
    return node < forward_.size();
  }
  auto availability(std::uint64_t) const -> NavigationAvailability override {
    ++availability_calls;
    return NavigationAvailability::Legal;
  }
  auto degree(std::uint64_t node, bool reverse) const -> std::size_t override {
    ++degree_calls;
    return (reverse ? reverse_ : forward_)[node].size();
  }
  auto edge(std::uint64_t node, std::size_t cursor, bool reverse) const
      -> NavigationLocalEdge override {
    ++edge_calls;
    return edges_[(reverse ? reverse_ : forward_)[node][cursor]];
  }
  auto complete() const noexcept -> bool override { return true; }
  mutable std::size_t edge_calls = 0, degree_calls = 0, availability_calls = 0;

 private:
  std::vector<NavigationLocalEdge> edges_;
  std::vector<std::vector<std::size_t>> forward_, reverse_;
};

TEST(NavigationOracle, ZeroBudgetAndHighDegreeProbeOnlyOneCandidatePerItem) {
  std::vector<NavigationLocalEdge> edges;
  for (std::size_t i = 0; i < 2000; ++i) edges.push_back({0, 1, i, 1});
  auto graph = std::make_shared<CountingDomain>(2, std::move(edges));
  NavigationQuery query(snapshot_of(graph), graph->location(0),
                        graph->location(1), NavigationLimits{4, 4, 4});
  EXPECT_EQ(query.advance({0}).items_done, 0u);
  EXPECT_EQ(graph->edge_calls, 0u);
  EXPECT_EQ(graph->degree_calls, 0u);
  EXPECT_EQ(graph->availability_calls, 0u);
  EXPECT_EQ(query.result(), nullptr);
  for (std::size_t i = 0;
       i < 10000 && query.state() == AsyncResultState::Pending; ++i) {
    const auto edge_calls = graph->edge_calls;
    const auto degree_calls = graph->degree_calls;
    EXPECT_EQ(query.advance({1}).items_done, 1u);
    EXPECT_LE(graph->edge_calls - edge_calls, 1u);
    EXPECT_LE(graph->degree_calls - degree_calls, 1u);
    if (query.state() == AsyncResultState::Pending) {
      EXPECT_EQ(query.result(), nullptr);
    }
  }
  ASSERT_NE(query.result(), nullptr);
  EXPECT_EQ(query.result()->outcome, NavigationOutcome::Found);
  EXPECT_EQ(graph->edge_calls, 2000u);
}

TEST(NavigationOracle, ReconstructionAndReversalRequireAdditionalItems) {
  constexpr std::size_t length = 60;
  std::vector<NavigationLocalEdge> edges;
  for (std::size_t i = 0; i < length; ++i) edges.push_back({i, i + 1, i, 1});
  auto graph = std::make_shared<CountingDomain>(length + 1, std::move(edges));
  NavigationQuery query(snapshot_of(graph), graph->location(0),
                        graph->location(length), kLimits);
  std::size_t remaining_calls = 0;
  for (std::size_t i = 0;
       i < 10000 && query.state() == AsyncResultState::Pending; ++i) {
    if (graph->edge_calls == length) ++remaining_calls;
    EXPECT_EQ(query.advance({1}).items_done, 1u);
  }
  ASSERT_NE(query.result(), nullptr);
  EXPECT_EQ(query.result()->route.size(), length);
  // Candidate enumeration has ended; extraction and reversal still need at
  // least one append per edge and one swap per pair, independent of reporting.
  EXPECT_GE(remaining_calls, length + length / 2);
}

TEST(NavigationOracle, RefusalNeverPublishesPartiallyBuiltProducts) {
  auto graph = std::make_shared<NavigationGraph>(
      4, std::vector<NavigationLocalEdge>{
             {0, 1, 1, 1}, {0, 2, 2, 1}, {1, 3, 3, 1}, {2, 3, 4, 1}});
  const auto snapshot = snapshot_of(graph);
  for (const auto limits :
       {NavigationLimits{1, 8, 8}, NavigationLimits{8, 1, 8},
        NavigationLimits{8, 8, 1}}) {
    NavigationQuery query(snapshot, graph->location(0), graph->location(3),
                          limits);
    const auto result = finish_query(query);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->outcome, NavigationOutcome::CapacityExceeded);
    EXPECT_TRUE(result->route.empty());
    EXPECT_TRUE(result->guidance.empty());
  }
  NavigationQuery reverse(snapshot, graph->location(3),
                          NavigationLimits{1, 8, 8});
  const auto result = finish_query(reverse);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::CapacityExceeded);
  EXPECT_TRUE(result->guidance.empty());
}

TEST(NavigationOracle, UnknownTopologyWeakensPositiveAndNegativeProof) {
  auto graph = std::make_shared<NavigationGraph>(
      3,
      std::vector<NavigationLocalEdge>{
          {0, 1, 1, 5}, {0, 2, 2, 0, 0, NavigationAvailability::Unavailable}});
  const auto snapshot = snapshot_of(graph);
  auto result = navigation_route(snapshot, graph->location(0),
                                 graph->location(1), kLimits);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::Found);
  EXPECT_EQ(result->proof, NavigationProof::FeasibleOnly);
  result = navigation_route(snapshot, graph->location(0), graph->location(2),
                            kLimits);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::Indeterminate);
  EXPECT_TRUE(result->route.empty());
}

TEST(NavigationOracle, CancelledAndSupersededRemainTerminalAfterReplacement) {
  auto graph = std::make_shared<NavigationGraph>(
      2, std::vector<NavigationLocalEdge>{{0, 1, 1, 1}});
  auto original = snapshot_of(graph);
  auto replacement = snapshot_of(graph);
  NavigationQuery cancelled(original, graph->location(0), graph->location(1));
  cancelled.advance({1});
  cancelled.cancel();
  cancelled.require_snapshot(replacement);
  EXPECT_EQ(cancelled.state(), AsyncResultState::Cancelled);
  EXPECT_EQ(cancelled.advance({1}).items_done, 0u);
  EXPECT_EQ(cancelled.result(), nullptr);
  NavigationQuery superseded(original, graph->location(0), graph->location(1));
  superseded.supersede();
  superseded.require_snapshot(replacement);
  EXPECT_EQ(superseded.state(), AsyncResultState::Superseded);
  EXPECT_EQ(superseded.advance({1}).items_done, 0u);
  EXPECT_EQ(superseded.result(), nullptr);
  NavigationQuery stale(original, graph->location(0), graph->location(1));
  stale.require_snapshot(replacement);
  EXPECT_EQ(stale.state(), AsyncResultState::Stale);
  EXPECT_EQ(stale.advance({1}).state, AsyncStepState::Stale);
  EXPECT_EQ(stale.result(), nullptr);
}

TEST(NavigationOracle, RetainedResultOwnsSnapshotAcrossScratchAndDomainDeath) {
  std::shared_ptr<const NavigationResult> retained;
  std::weak_ptr<const NavigationDomain> lifetime;
  NavigationLocation start{}, goal{};
  {
    auto graph = std::make_shared<NavigationGraph>(
        2, std::vector<NavigationLocalEdge>{{0, 1, 7, 3}});
    lifetime = graph;
    start = graph->location(0);
    goal = graph->location(1);
    auto snapshot = snapshot_of(graph);
    NavigationQuery query(snapshot, start, goal, kLimits);
    retained = finish_query(query);
    ASSERT_NE(retained, nullptr);
    EXPECT_TRUE(retained->current_for(snapshot));
  }
  EXPECT_FALSE(lifetime.expired());
  ASSERT_EQ(retained->route.size(), 1u);
  EXPECT_EQ(retained->route[0].from, start);
  EXPECT_EQ(retained->route[0].to, goal);
  auto replacement_graph = std::make_shared<NavigationGraph>(
      2, std::vector<NavigationLocalEdge>{{0, 1, 7, 3}});
  EXPECT_NE(replacement_graph->identity(), start.domain);
  auto replacement = snapshot_of(replacement_graph);
  EXPECT_FALSE(retained->current_for(replacement));
  EXPECT_FALSE(replacement->contains(start));
  retained.reset();
  EXPECT_TRUE(lifetime.expired());
}

TEST(NavigationOracle, DestroyedDomainAddressReuseRejectsOldLocations) {
  alignas(NavigationGraph) std::byte storage[sizeof(NavigationGraph)];
  auto* first = std::construct_at(reinterpret_cast<NavigationGraph*>(storage),
                                  1, std::vector<NavigationLocalEdge>{});
  const auto old_location = first->location(0);
  const auto* old_address = first;
  std::destroy_at(first);
  auto* second = std::construct_at(reinterpret_cast<NavigationGraph*>(storage),
                                   1, std::vector<NavigationLocalEdge>{});
  // Shared ownership ends before the stack storage does, also on ASSERT return.
  std::shared_ptr<const NavigationDomain> replacement(
      second, [](const NavigationDomain* value) {
        std::destroy_at(static_cast<const NavigationGraph*>(value));
      });
  ASSERT_EQ(second, old_address);
  EXPECT_NE(second->identity(), old_location.domain);
  auto snapshot = snapshot_of(replacement);
  EXPECT_FALSE(snapshot->contains(old_location));
  EXPECT_TRUE(snapshot->contains(second->location(0)));
  auto result =
      navigation_route(snapshot, old_location, second->location(0), kLimits);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::InvalidInput);
}

TEST(NavigationOracle, DomainDiagnosticsPrecedeAttachmentFailures) {
  auto graph =
      std::make_shared<NavigationGraph>(2, std::vector<NavigationLocalEdge>{});
  auto invalid = std::make_shared<NavigationGraph>(
      1, std::vector<NavigationLocalEdge>{{0, 2, 1, 1}});
  for (const auto reason : {NavigationPreparationError::NullDomain,
                            NavigationPreparationError::InvalidDomain,
                            NavigationPreparationError::DuplicateDomain}) {
    std::vector<std::shared_ptr<const NavigationDomain>> domains{graph};
    domains.push_back(reason == NavigationPreparationError::NullDomain ? nullptr
                      : reason == NavigationPreparationError::InvalidDomain
                          ? invalid
                          : graph);
    NavigationSnapshot snapshot(
        domains, {{graph->location(0), graph->location(1), 99, kReserved}});
    EXPECT_FALSE(snapshot.valid());
    EXPECT_EQ(snapshot.diagnostic().reason, reason);
    EXPECT_EQ(snapshot.diagnostic().index, 1u);
    EXPECT_EQ(snapshot.diagnostic().id,
              domains[1] ? domains[1]->identity() : 0u);
  }
}

TEST(NavigationOracle, AttachmentDiagnosticsIdentifyFirstFailure) {
  auto graph =
      std::make_shared<NavigationGraph>(2, std::vector<NavigationLocalEdge>{});
  const auto from = graph->location(0);
  const auto to = graph->location(1);
  const std::vector<NavigationConnection> valid{{from, to, 70, 1}};
  for (const auto reason :
       {NavigationPreparationError::InvalidConnectionFrom,
        NavigationPreparationError::InvalidConnectionTo,
        NavigationPreparationError::ReservedConnectionCost,
        NavigationPreparationError::DuplicateConnectionId}) {
    auto connections = valid;
    auto broken = NavigationConnection{from, to, 71, 2};
    if (reason == NavigationPreparationError::InvalidConnectionFrom)
      broken.from.node = 9;
    if (reason == NavigationPreparationError::InvalidConnectionTo)
      broken.to.domain = 0;
    if (reason == NavigationPreparationError::ReservedConnectionCost)
      broken.cost = kReserved;
    if (reason == NavigationPreparationError::DuplicateConnectionId)
      broken.id = 70;
    connections.push_back(broken);
    connections.push_back({from, to, 72, kReserved});
    auto snapshot = std::make_shared<NavigationSnapshot>(
        std::vector<std::shared_ptr<const NavigationDomain>>{graph},
        connections);
    EXPECT_FALSE(snapshot->valid());
    EXPECT_EQ(snapshot->diagnostic().reason, reason);
    EXPECT_EQ(snapshot->diagnostic().index, 1u);
    EXPECT_EQ(snapshot->diagnostic().id, broken.id);
    auto result = navigation_route(snapshot, from, to, kLimits);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->outcome, NavigationOutcome::InvalidInput);
  }
  auto good = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph}, valid);
  EXPECT_TRUE(good->valid());
  EXPECT_EQ(good->diagnostic().reason, NavigationPreparationError::None);
}

TEST(NavigationOracle,
     QueueLifecycleAndRetentionAreSeparateFromSpatialOutcome) {
  auto graph = std::make_shared<NavigationGraph>(
      3, std::vector<NavigationLocalEdge>{{0, 1, 1, 1}});
  auto snapshot = snapshot_of(graph);
  NavigationQuery query(snapshot, graph->location(0), graph->location(2),
                        kLimits);
  ResumableWorkQueue<std::shared_ptr<const NavigationResult>> queue;
  const auto ticket = queue.submit(query);
  for (std::size_t i = 0;
       i < 1000 && queue.state(ticket) == AsyncResultState::Pending; ++i)
    (void)queue.advance({1});
  ASSERT_EQ(queue.state(ticket), AsyncResultState::Ready);
  const auto* slot = queue.result(ticket);
  ASSERT_NE(slot, nullptr);
  auto retained = *slot;
  ASSERT_NE(retained, nullptr);
  EXPECT_EQ(retained->outcome, NavigationOutcome::NoRoute);
  queue.clear();
  EXPECT_EQ(queue.state(ticket), AsyncResultState::Unbound);
  EXPECT_EQ(retained->outcome, NavigationOutcome::NoRoute);
  NavigationQuery cancelled(snapshot, graph->location(0), graph->location(1));
  const auto cancelled_ticket = queue.submit(cancelled);
  EXPECT_TRUE(queue.cancel(cancelled_ticket));
  EXPECT_EQ(queue.state(cancelled_ticket), AsyncResultState::Cancelled);
  EXPECT_EQ(queue.result(cancelled_ticket), nullptr);
}

TEST(NavigationOracle, DomainReentryPreservesDirectedConnectionIdentity) {
  auto first = std::make_shared<NavigationGraph>(
      3, std::vector<NavigationLocalEdge>{{0, 1, 1, 10}, {0, 2, 2, 30}});
  auto second = std::make_shared<NavigationGraph>(
      2, std::vector<NavigationLocalEdge>{{0, 1, 1, 2}});
  auto snapshot = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{first, second},
      std::vector<NavigationConnection>{
          {first->location(1), second->location(0), 41, 5, 401},
          {first->location(1), second->location(0), 42, 0, 402},
          {second->location(1), first->location(2), 43, 0, 403}});
  const auto result = navigation_route(snapshot, first->location(0),
                                       first->location(2), kLimits);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(result->outcome, NavigationOutcome::Found);
  EXPECT_EQ(result->cost, 12u);
  ASSERT_EQ(result->route.size(), 4u);
  EXPECT_EQ(result->route[0].from.domain, first->identity());
  EXPECT_EQ(result->route[1].id, 42u);
  EXPECT_EQ(result->route[1].key, 402u);
  EXPECT_TRUE(result->route[1].connection);
  EXPECT_EQ(result->route[2].from.domain, second->identity());
  EXPECT_EQ(result->route[3].id, 43u);
  EXPECT_EQ(result->route[3].to.domain, first->identity());
  NavigationQuery reverse(snapshot, first->location(2), kLimits);
  const auto product = finish_query(reverse);
  ASSERT_NE(product, nullptr);
  const auto* at = product->guidance_at(first->location(0));
  ASSERT_NE(at, nullptr);
  EXPECT_EQ(at->cost, 12u);
  auto backward = navigation_route(snapshot, first->location(2),
                                   first->location(0), kLimits);
  ASSERT_NE(backward, nullptr);
  EXPECT_EQ(backward->outcome, NavigationOutcome::NoRoute);
}

TEST(NavigationOracle, InvalidDomainsCannotHideBehindOtherValidEndpoints) {
  auto valid = std::make_shared<NavigationGraph>(
      2, std::vector<NavigationLocalEdge>{{0, 1, 1, 1}});
  auto invalid = std::make_shared<NavigationGraph>(
      2, std::vector<NavigationLocalEdge>{{0, 1, 7, 1}, {1, 0, 7, 1}});
  auto snapshot = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{valid, invalid});
  EXPECT_FALSE(snapshot->valid());
  const auto result = navigation_route(snapshot, valid->location(0),
                                       valid->location(1), kLimits);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, NavigationOutcome::InvalidInput);
  EXPECT_TRUE(result->route.empty());
  NavigationQuery empty(snapshot_of(valid), std::vector<NavigationGoal>{},
                        kLimits);
  const auto empty_result = finish_query(empty);
  ASSERT_NE(empty_result, nullptr);
  EXPECT_EQ(empty_result->outcome, NavigationOutcome::InvalidInput);
  EXPECT_TRUE(empty_result->guidance.empty());
}

TEST(NavigationOracle,
     ProfileAndOffRouteShortcutInvalidatePositiveAndNegative) {
  auto graph = std::make_shared<NavigationGraph>(
      3, std::vector<NavigationLocalEdge>{
             {0, 1, 1, 10}, {0, 2, 2, 3}, {2, 1, 3, 20}});
  auto original = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph},
      std::vector<NavigationConnection>{}, 7);
  NavigationQuery query(original, graph->location(0), graph->location(1),
                        kLimits);
  auto result = finish_query(query);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->cost, 10u);
  auto policy = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph},
      std::vector<NavigationConnection>{}, 8);
  EXPECT_FALSE(result->current_for(policy));
  auto shortcut = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph},
      std::vector<NavigationConnection>{
          {graph->location(2), graph->location(1), 1, 0}},
      7);
  EXPECT_FALSE(result->current_for(shortcut));
  auto improved = navigation_route(shortcut, graph->location(0),
                                   graph->location(1), kLimits);
  ASSERT_NE(improved, nullptr);
  EXPECT_EQ(improved->cost, 3u);
  query.require_snapshot(shortcut);
  EXPECT_EQ(query.state(), AsyncResultState::Stale);
  EXPECT_EQ(query.result(), nullptr);
  EXPECT_EQ(result->cost, 10u);
  auto negative = navigation_route(original, graph->location(1),
                                   graph->location(0), kLimits);
  ASSERT_NE(negative, nullptr);
  EXPECT_EQ(negative->outcome, NavigationOutcome::NoRoute);
  auto joined = std::make_shared<NavigationSnapshot>(
      std::vector<std::shared_ptr<const NavigationDomain>>{graph},
      std::vector<NavigationConnection>{
          {graph->location(1), graph->location(0), 1, 0}},
      7);
  EXPECT_FALSE(negative->current_for(joined));
  auto now_reachable =
      navigation_route(joined, graph->location(1), graph->location(0), kLimits);
  ASSERT_NE(now_reachable, nullptr);
  EXPECT_EQ(now_reachable->outcome, NavigationOutcome::Found);
}

TEST(NavigationOracle, QueueSupersessionAndStaleContinuationPublishNothing) {
  auto graph = std::make_shared<NavigationGraph>(
      2, std::vector<NavigationLocalEdge>{{0, 1, 1, 1}});
  auto original = snapshot_of(graph);
  auto replacement = snapshot_of(graph);
  NavigationQuery superseded(original, graph->location(0), graph->location(1));
  NavigationQuery stale(original, graph->location(0), graph->location(1));
  ResumableWorkQueue<std::shared_ptr<const NavigationResult>> queue;
  const auto superseded_ticket = queue.submit(superseded);
  const auto stale_ticket = queue.submit(stale);
  EXPECT_TRUE(queue.supersede(superseded_ticket));
  stale.require_snapshot(replacement);
  const auto step = queue.advance({1});
  EXPECT_EQ(step.items_done, 0u);
  EXPECT_EQ(queue.state(superseded_ticket), AsyncResultState::Superseded);
  EXPECT_EQ(queue.state(stale_ticket), AsyncResultState::Stale);
  EXPECT_EQ(queue.result(superseded_ticket), nullptr);
  EXPECT_EQ(queue.result(stale_ticket), nullptr);
  EXPECT_EQ(superseded.result(), nullptr);
  EXPECT_EQ(stale.result(), nullptr);
}

}  // namespace

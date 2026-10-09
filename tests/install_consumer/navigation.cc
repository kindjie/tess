// Self-contained installed graph/grid/grid consumer; no source-tree headers.
#include <tess/navigation/grid.h>
#include <tess/navigation/query.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {
struct Walkable {};
struct Weight {};
using HallShape = tess::Shape<tess::Extent3{4, 1, 1}, tess::Extent3{2, 1, 1}>;
using FloorShape = tess::Shape<tess::Extent3{2, 2, 1}, tess::Extent3{2, 2, 1}>;
using Hall =
    tess::AlwaysResidentWorld<HallShape,
                              tess::FieldSchema<tess::Field<Walkable, bool>>>;
using Floor = tess::AlwaysResidentWorld<
    FloorShape, tess::FieldSchema<tess::Field<Weight, std::uint32_t>>>;
using HallMovement = tess::movement::UnitCostFieldMovement<Walkable>;
using FloorMovement =
    tess::movement::MovementClass<tess::movement::NotZero<Weight>,
                                  tess::movement::FieldCost<Weight>>;
}  // namespace

int main() {
  auto hall_world = std::make_shared<Hall>();
  hall_world->fill_field<Walkable>(true);
  auto floor_world = std::make_shared<Floor>();
  floor_world->fill_field<Weight>(1);
  auto hall = std::make_shared<tess::NavigationGrid<Hall, HallMovement>>(
      std::move(hall_world), 2);
  auto floor = std::make_shared<tess::NavigationGrid<Floor, FloorMovement>>(
      std::move(floor_world), 3);
  auto road = std::make_shared<tess::NavigationGraph>(
      2, std::vector<tess::NavigationLocalEdge>{{0, 1, 1, 5}, {1, 0, 2, 7}});
  const auto entry = hall->coordinate_location({0, 0, 0});
  const auto exit = hall->coordinate_location({3, 0, 0});
  const auto seam = floor->coordinate_location({0, 0, 0});
  const auto goal = floor->coordinate_location({1, 1, 0});
  auto snapshot = std::make_shared<tess::NavigationSnapshot>(
      std::vector<std::shared_ptr<const tess::NavigationDomain>>{road, hall,
                                                                 floor},
      std::vector<tess::NavigationConnection>{{road->location(1), entry, 10, 0},
                                              {entry, road->location(1), 11, 0},
                                              {exit, seam, 12, 0},
                                              {seam, exit, 13, 0}});
  const auto forward =
      tess::navigation_route(snapshot, road->location(0), goal);
  const auto reverse =
      tess::navigation_route(snapshot, goal, road->location(0));
  if (!forward || !reverse ||
      forward->outcome != tess::NavigationOutcome::Found ||
      reverse->outcome != tess::NavigationOutcome::Found ||
      forward->cost != 17 || reverse->cost != 19 ||
      forward->route.size() != 8 || !forward->route[1].connection ||
      forward->route[1].id != 10 || !forward->route[5].connection ||
      forward->route[5].id != 12)
    return 1;
  tess::NavigationQuery shared(snapshot, goal,
                               tess::NavigationLimits{16, 32, 16});
  tess::ResumableWorkQueue<std::shared_ptr<const tess::NavigationResult>> queue;
  const auto ticket = queue.submit(shared);
  while (queue.state(ticket) == tess::AsyncResultState::Pending)
    (void)queue.advance({1});
  const auto* published = queue.result(ticket);
  if (!published || !*published) return 2;
  const auto* guidance = (*published)->guidance_at(road->location(0));
  if (!guidance || guidance->cost != forward->cost) return 3;
  std::cout << "composable navigation: OK (forward 17, return 19)\n";
}

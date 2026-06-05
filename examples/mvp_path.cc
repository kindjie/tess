#include <tess/tess.h>

#include <cstdint>
#include <iostream>

namespace {

struct PassableTag {};

constexpr std::uint32_t DirtyPassability = 1u << 0u;

using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

}  // namespace

int main() {
  World world;
  tess::FrameOps ops;

  (void)ops.update_field(
      tess::DomainDesc::resident_chunks(),
      tess::FieldAccessDesc{0, DirtyPassability, DirtyPassability},
      tess::WritePolicy::UniquePerChunk);

  const auto report = tess::plan_operations(world, ops);
  if (!report.ok()) {
    std::cerr << "planning failed\n";
    return 1;
  }

  const auto execution = tess::execute_plan<tess::WritePolicy::UniquePerChunk>(
      world, report.plan(), [](auto view) {
        auto passable = view.template field_span<PassableTag>();
        view.for_each_tile([&](tess::LocalTileId id, tess::LocalCoord3 local) {
          const auto coord = view.world_coord(local);
          passable[id.value] = coord.x == 1 && coord.y < 3 ? 0u : 1u;
        });
      });
  if (execution.status != tess::PlannedExecutionStatus::Executed) {
    std::cerr << "execution failed\n";
    return 1;
  }

  tess::PathScratch scratch;
  scratch.reserve_nodes(Shape::size.x * Shape::size.y * Shape::size.z);

  const auto path = tess::astar_path<World, PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 0, 0}},
      scratch);
  if (path.status != tess::PathStatus::Found) {
    std::cerr << "path failed\n";
    return 1;
  }

  std::cout << "path cost: " << path.cost << "\n";
  return 0;
}

#include <tess/path/path.h>
#include <tess/storage/world.h>

#include <cstdint>

struct PassableTag {};
struct CostTag {};
using WeightedMovement =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;

using Shape = tess::Shape<tess::Extent3{4, 4, 1}, tess::Extent3{2, 2, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

int main() {
  World world;
  tess::DistanceFieldScratch scratch;
  static_cast<void>(
      tess::build_bounded_weighted_distance_field<World, WeightedMovement, 0>(
          world, {}, scratch));
}

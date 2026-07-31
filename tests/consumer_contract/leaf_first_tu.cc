// A consumer that never includes an umbrella header: it reaches
// straight for the narrowest owning header, which the packaging guide
// recommends. Nothing here may depend on tess.h, pathfinding.h, or
// simulation.h having been included first.
#include <tess/path/path.h>
#include <tess/storage/world.h>
#include <tess/topology/topology.h>

#include "contract.h"

namespace tess_test::contract {

namespace {

struct LeafTag {};
using LeafShape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using LeafSchema = tess::FieldSchema<tess::Field<LeafTag, bool>>;

}  // namespace

// Exercised for its compilation, and linked so an accidental
// non-inline definition in these headers still surfaces.
auto leaf_first_world_is_usable() -> bool {
  tess::AlwaysResidentWorld<LeafShape, LeafSchema> world;
  world.field<LeafTag>(tess::Coord3{0, 0, 0}) = true;
  return world.field<LeafTag>(tess::Coord3{0, 0, 0});
}

}  // namespace tess_test::contract

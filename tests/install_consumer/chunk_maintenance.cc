#include <tess/maintenance/chunk_adapter.h>

#include <cstdint>

namespace {

namespace maintenance = tess::maintenance;

struct ValueTag {};
using Schema = tess::FieldSchema<tess::Field<ValueTag, std::uint16_t>>;
using Shape = tess::Shape<tess::Extent3{2, 1, 1}, tess::Extent3{2, 1, 1}>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

struct Summary {
  std::uint32_t sum = 0;
};

struct RebuildSummary {
  void operator()(const World& world, tess::ChunkKey key,
                  tess::DirtyObservation, Summary& summary) const {
    summary = {};
    for (const auto value : world.field_span<ValueTag>(key)) {
      summary.sum += value;
    }
  }
};

}  // namespace

int main() {
  constexpr auto value_dirty = tess::DirtyMask{1};
  World world;
  maintenance::ChunkMaintenanceAdapter<World, Summary, RebuildSummary> adapter{
      world, value_dirty, RebuildSummary{}};
  const auto key = tess::ChunkKey{0};

  world.field<ValueTag>(tess::Coord3{0, 0, 0}) = 17;
  if (adapter.mark_dirty(
          key, value_dirty,
          tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}}) !=
          maintenance::ChunkMarkResult::Accepted ||
      adapter.flush() != maintenance::DrainResult::Idle) {
    return 1;
  }
  const auto product = adapter.product(key);
  return product.state == maintenance::ChunkProductState::Current &&
                 product.value != nullptr && product.value->sum == 17 &&
                 product.token.content_version ==
                     world.meta(key).content_version &&
                 world.dirty_mask(key).empty() && adapter.current(product.token)
             ? 0
             : 1;
}

// External chunk maintenance: rebuild a versioned derived product without
// embedding scheduler state in the world.
// Self-checking: returns nonzero on any failed contract.

#include <tess/core/config.h>
#include <tess/maintenance/chunk_adapter.h>

#include <cstdint>
#include <exception>
#include <iostream>

namespace {

namespace maintenance = tess::maintenance;

struct HeightTag {};
using Schema = tess::FieldSchema<tess::Field<HeightTag, std::uint16_t>>;
using Shape = tess::Shape<tess::Extent3{8, 4, 1}, tess::Extent3{4, 4, 1}>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

struct HeightSummary {
  std::uint64_t sum = 0;
  std::uint32_t nonzero = 0;
};

struct RebuildHeightSummary {
  void operator()(const World& world, tess::ChunkKey key,
                  tess::DirtyObservation, HeightSummary& summary) const {
    summary = {};
    for (const auto value : world.field_span<HeightTag>(key)) {
      summary.sum += value;
      summary.nonzero += value != 0 ? 1u : 0u;
    }
  }
};

auto run_example() -> bool {
  constexpr auto height_dirty = tess::DirtyMask{1};
  World world;
  maintenance::ChunkMaintenanceAdapter<World, HeightSummary,
                                       RebuildHeightSummary>
      adapter{world, height_dirty, RebuildHeightSummary{}};
  const auto key = tess::ChunkKey{0};

  for (std::uint16_t value = 1; value <= 32; ++value) {
    world.field<HeightTag>(tess::Coord3{value % 4, value % 3, 0}) = value;
    const auto marked =
        adapter.mark_dirty(key, height_dirty,
                           tess::Box3{tess::Coord3{value % 4, value % 3, 0},
                                      tess::Extent3{1, 1, 1}});
    if (marked != maintenance::ChunkMarkResult::Accepted) {
      return false;
    }
  }

  // The stable default executes each offer synchronously. The explicit flush
  // confirms the adapter is idle before a consumer depends on the product.
  if (adapter.flush() != maintenance::DrainResult::Idle) {
    return false;
  }
  const auto product = adapter.product(key);
  const auto metrics = adapter.metrics();
  if (product.state != maintenance::ChunkProductState::Current ||
      product.value == nullptr || product.value->sum == 0 ||
      product.token.content_version != world.meta(key).content_version ||
      !world.dirty_mask(key).empty() || metrics.schedule_calls != 32 ||
      metrics.executions != 32) {
    return false;
  }

  std::cout << "offers=" << metrics.schedule_calls
            << " rebuilds=" << metrics.executions
            << " content_version=" << product.token.content_version.value
            << " sum=" << product.value->sum << '\n';
  return true;
}

}  // namespace

int main() {
#if TESS_HAS_EXCEPTIONS
  try {
#endif
    if (!run_example()) {
      std::cerr << "chunk_maintenance example failed\n";
      return 1;
    }
#if TESS_HAS_EXCEPTIONS
  } catch (const std::exception& error) {
    std::cerr << "chunk_maintenance example failed: " << error.what() << '\n';
    return 1;
  }
#endif
  return 0;
}

// Budget-bounded sparse residency: a large world where only touched chunks
// materialize, and a path query that reports Indeterminate until the missing
// chunk is streamed in. Self-checking: returns nonzero on any failed check.

#include <tess/pathfinding.h>

#include <cstdint>
#include <iostream>

namespace {

struct PassableTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;

// Act 1: footprint. 1,024 chunks dense vs a 16-page budget.
using HugeShape =
    tess::Shape<tess::Extent3{1024, 1024, 1}, tess::Extent3{32, 32, 1}>;
using HugeSparse = tess::SparseResidentWorld<HugeShape, Schema>;
using HugeDense = tess::AlwaysResidentWorld<HugeShape, Schema>;

auto stay_within_budget() -> bool {
  // [sparse-budget]
  constexpr auto budget = 16 * HugeSparse::page_byte_size;
  HugeSparse world{tess::ResidencyConfig{budget}};  // Loads no chunks.

  for (std::uint64_t key = 0; key < 64; ++key) {
    (void)world.ensure_resident(tess::ChunkKey{key});  // LRU-evicts at cap.
  }

  static_assert(HugeDense::storage_byte_size == 64 * budget);
  const auto ok = world.resident_byte_size() <= world.byte_budget();
  // [sparse-budget]

  std::cout << "dense bytes: " << HugeDense::storage_byte_size
            << ", resident bytes: " << world.resident_byte_size() << "\n";
  return ok && world.resident_count() == world.capacity();
}

// Act 2: streaming. Three chunks in a row; the bridge chunk is missing.
using StreamShape =
    tess::Shape<tess::Extent3{96, 32, 1}, tess::Extent3{32, 32, 1}>;
using StreamWorld = tess::SparseResidentWorld<StreamShape, Schema>;

void open_row(StreamWorld& world, std::int64_t from_x, std::int64_t to_x) {
  for (std::int64_t x = from_x; x <= to_x; ++x) {
    world.field<PassableTag>(tess::Coord3{x, 0, 0}) = 1;
  }
}

auto stream_the_bridge() -> bool {
  StreamWorld world{tess::ResidencyConfig{3 * StreamWorld::page_byte_size}};
  (void)world.ensure_resident(tess::ChunkKey{0});
  (void)world.ensure_resident(tess::ChunkKey{2});
  open_row(world, 0, 31);
  open_row(world, 64, 95);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{95, 0, 0}};

  // [sparse-indeterminate]
  tess::PathScratch scratch;
  const auto blocked = tess::astar_path<StreamWorld, PassableTag>(
      world, request, scratch, tess::MissingChunkPolicy::Indeterminate);
  // blocked.status == PathStatus::Indeterminate: a non-resident chunk was
  // skipped, so failure is not proven -- stream the chunk in and retry.

  (void)world.ensure_resident(tess::ChunkKey{1});
  open_row(world, 32, 63);
  const auto found = tess::astar_path<StreamWorld, PassableTag>(
      world, request, scratch, tess::MissingChunkPolicy::Indeterminate);
  // [sparse-indeterminate]

  return blocked.status == tess::PathStatus::Indeterminate &&
         found.status == tess::PathStatus::Found && found.cost == 95;
}

}  // namespace

int main() {
  if (!stay_within_budget() || !stream_the_bridge()) {
    std::cerr << "sparse_stream example failed\n";
    return 1;
  }
  return 0;
}

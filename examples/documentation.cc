// Maintained Markdown copies named regions from this compiled, self-checking
// example. Keep setup outside regions when it would distract from the concept
// being documented.

// [getting-pathfinding-include]
#include <tess/pathfinding.h>
// [getting-pathfinding-include]

// [getting-simulation-include]
#include <tess/simulation.h>
// [getting-simulation-include]

#include <array>
#include <cstdint>
#include <iostream>

namespace {

// [getting-shape]
using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
// [getting-shape]

// [getting-schema]
struct PassableTag {};
struct CostTag {};
struct ConstructionTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                 tess::Field<CostTag, std::uint32_t>,
                                 tess::Field<ConstructionTag, std::uint8_t>>;
// [getting-schema]

// [getting-world]
using World = tess::AlwaysResidentWorld<Shape, Schema>;
World world;  // Allocates every chunk; all fields are zero-initialized.
// [getting-world]

constexpr std::uint32_t kTerrainDirty = 1u << 0u;

void write_one_tile() {
  // [getting-direct-write]
  world.field<PassableTag>(tess::Coord3{4, 2, 0}) = 1;
  // [getting-direct-write]
}

auto find_path() -> bool {
  for (std::int64_t x = 0; x < static_cast<std::int64_t>(Shape::size.x); ++x) {
    world.field<PassableTag>(tess::Coord3{x, 0, 0}) = 1;
  }
  const auto start = tess::Coord3{0, 0, 0};
  const auto goal = tess::Coord3{Shape::size.x - 1, 0, 0};

  // [getting-astar]
  tess::PathScratch scratch;
  const auto result = tess::astar_path<World, PassableTag>(
      world, tess::PathRequest{start, goal}, scratch);
  // [getting-astar]

  return result.status == tess::PathStatus::Found;
}

// [getting-movement-class]
using Walker = tess::movement::MovementClass<
    tess::movement::AllOf<
        tess::movement::Field<PassableTag>,
        tess::movement::Not<tess::movement::Field<ConstructionTag>>>,
    tess::movement::FieldCost<CostTag>>;
// [getting-movement-class]

auto check_topology() -> bool {
  const auto start = tess::Coord3{0, 0, 0};
  const auto goal = tess::Coord3{Shape::size.x - 1, 0, 0};
  tess::RegionGraphScratch precheck_scratch;

  // [getting-topology]
  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  tess::build_region_graph<World, Walker>(world, scratch, graph);

  const auto verdict =
      tess::precheck_path<Walker>(graph, world, start, goal, precheck_scratch);
  // [getting-topology]

  return verdict == tess::PrecheckStatus::Reachable;
}

struct BuildTask {
  auto operator()(const tess::ScheduleTaskContext&)
      -> tess::ScheduleTaskResult {
    return {.dirty_mask = kTerrainDirty};
  }
};

struct TopologyTask {
  auto operator()(const tess::ScheduleTaskContext&)
      -> tess::ScheduleTaskResult {
    return {};
  }
};

auto run_schedule() -> bool {
  BuildTask build_task;
  TopologyTask topology_task;

  // [getting-schedule]
  tess::Schedule schedule;
  schedule.add_task(
      {"build", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()},
      build_task);
  schedule.add_task({"topology", tess::SimPhase::Topology,
                     tess::Cadence::on_dirty(kTerrainDirty)},
                    topology_task);
  schedule.seal();

  tess::SimClock clock;
  tess::FixedStepAccumulator accumulator(20, 8);
  tess::run_schedule_frame(schedule, clock, accumulator, 1.0 / 20.0,
                           tess::SimTimeControl{tess::SimSpeed::Speed1x});
  // [getting-schedule]

  return clock.tick == 1;
}

auto collect_deltas() -> bool {
  tess::DeltaCollector deltas;
  deltas.reserve(World::chunk_count, 1, 0);
  world.mark_dirty(tess::ChunkKey{0}, kTerrainDirty,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});

  // [getting-render-deltas]
  tess::collect_tile_deltas(deltas, world, kTerrainDirty);
  const auto frame = deltas.publish();
  // [getting-render-deltas]

  return !frame.chunks.empty();
}

struct TerrainTag {};
using MyShape = Shape;
using MySchema = tess::FieldSchema<tess::Field<TerrainTag, std::uint8_t>,
                                   tess::Field<CostTag, float>>;

// [storage-field-array]
template <typename Value>
using ChunkFieldStorage =
    std::array<Value, tess::ShapeTraits<MyShape>::local_tile_count>;
// [storage-field-array]

auto access_chunk_page() -> bool {
  tess::ChunkPage<MyShape, MySchema> page{tess::ChunkKey{0},
                                          tess::ChunkCoord3{0, 0, 0}};

  // [storage-page-access]
  auto terrain = page.field_span<TerrainTag>();
  page.field<CostTag>(tess::LocalTileId{42}) = 3.0F;
  // [storage-page-access]

  return terrain.size() == MyShape::chunk.x * MyShape::chunk.y;
}

auto access_dense_world() -> bool {
  // [storage-dense-world]
  tess::AlwaysResidentWorld<MyShape, MySchema> dense_world;
  auto pages = dense_world.chunks();
  auto& page = dense_world.chunk(tess::ChunkKey{3});
  auto resolved = dense_world.resolve(tess::Coord3{10, 20, 0});
  dense_world.field<TerrainTag>(tess::Coord3{10, 20, 0}) = 7;
  auto terrain = dense_world.field_span<TerrainTag>(tess::ChunkKey{3});
  // [storage-dense-world]

  return pages.size() ==
             tess::AlwaysResidentWorld<MyShape, MySchema>::chunk_count &&
         page.chunk_key() == tess::ChunkKey{3} &&
         resolved.chunk_key.value <
             tess::AlwaysResidentWorld<MyShape, MySchema>::chunk_count &&
         !terrain.empty();
}

using HugeShape = Shape;

auto access_sparse_world() -> bool {
  using SparseWorld = tess::SparseResidentWorld<HugeShape, MySchema>;
  constexpr auto budget = 16 * SparseWorld::page_byte_size;

  // [storage-sparse-world]
  tess::SparseResidentWorld<HugeShape, MySchema> sparse_world{
      tess::ResidencyConfig{budget}};
  const auto handle = sparse_world.ensure_resident(tess::ChunkKey{0});
  sparse_world.chunk(tess::ChunkKey{0})
      .field<TerrainTag>(tess::LocalTileId{0}) = 7;
  // [storage-sparse-world]

  return sparse_world.valid(handle);
}

auto access_chunk_metadata() -> bool {
  tess::AlwaysResidentWorld<MyShape, MySchema> dense_world;

  // [storage-metadata]
  auto& meta = dense_world.meta(tess::ChunkKey{3});
  auto* checked = dense_world.try_meta(tess::ChunkCoord3{3, 0, 0});
  // [storage-metadata]

  return &meta == checked;
}

auto plan_weighted_batch() -> bool {
  for (std::int64_t y = 0; y < static_cast<std::int64_t>(Shape::size.y); ++y) {
    for (std::int64_t x = 0; x < static_cast<std::int64_t>(Shape::size.x);
         ++x) {
      world.field<PassableTag>(tess::Coord3{x, y, 0}) = 1;
      world.field<CostTag>(tess::Coord3{x, y, 0}) = 1;
    }
  }

  // [path-batch]
  tess::WeightedPathBatchScratch scratch;
  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 31, 0}},
      tess::PathRequest{tess::Coord3{0, 1, 0}, tess::Coord3{31, 31, 0}},
      tess::PathRequest{tess::Coord3{0, 2, 0}, tess::Coord3{31, 31, 0}},
  };
  const auto results =
      tess::weighted_path_batch<World, PassableTag, CostTag, /*MaxCost=*/128>(
          world, requests, scratch);
  // [path-batch]

  if (results.size() != requests.size()) {
    return false;
  }
  for (const auto& result : results) {
    if (result.status != tess::PathStatus::Found) {
      return false;
    }
  }
  const auto stats = scratch.stats();
  return stats.requests == requests.size() && stats.unique_goals == 1u;
}

auto reuse_cached_route() -> bool {
  // [route-cache]
  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 31, 0}};

  const auto miss = tess::cached_astar_path<World, PassableTag>(world, request,
                                                                scratch, cache);
  const auto hit = tess::cached_astar_path<World, PassableTag>(
      world, request, scratch, cache);  // Served from cache: no expansion.
  // [route-cache]

  return miss.status == tess::PathStatus::Found &&
         hit.status == tess::PathStatus::Found && miss.expanded_nodes > 0u &&
         hit.expanded_nodes == 0u && cache.stats().hits == 1u;
}

auto share_field_product() -> bool {
  // [field-product]
  tess::GoalSet goals;
  goals.add(tess::Coord3{31, 0, 0});
  goals.add(tess::Coord3{31, 31, 0});

  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  const auto built = tess::build_distance_field_product<World, PassableTag>(
      world, goals, scratch, product);

  tess::FieldProductCache cache{1u << 20u};  // Byte-budgeted.
  const auto stored = cache.store<World, PassableTag>(std::move(product));
  const auto* shared = cache.lookup<World, PassableTag>(world, goals);
  if (shared == nullptr) {
    return false;
  }

  const auto nearest = tess::nearest_target<World, PassableTag>(
      world, tess::Coord3{0, 31, 0}, *shared, scratch);
  // [field-product]

  return built.status == tess::PathStatus::Found && stored &&
         nearest.status == tess::PathStatus::Found;
}

}  // namespace

int main() {
  const auto ok = find_path() && check_topology() && run_schedule() &&
                  collect_deltas() && access_chunk_page() &&
                  access_dense_world() && access_sparse_world() &&
                  access_chunk_metadata() && plan_weighted_batch() &&
                  reuse_cached_route() && share_field_product();
  if (!ok) {
    std::cerr << "documentation example failed\n";
    return 1;
  }
  write_one_tile();
  return 0;
}

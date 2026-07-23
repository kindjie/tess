#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstdint>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
using Shape = tess::Shape<tess::Extent3{8, 4, 1}, tess::Extent3{4, 4, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

template <typename WorldType>
void fill_open(WorldType& world) {
  for (auto& chunk : world.chunks()) {
    auto values = chunk.template field_span<PassableTag>();
    std::fill(values.begin(), values.end(), 1);
  }
}

auto graph_for(World& world) -> tess::RegionGraph {
  tess::LocalTopologyScratch scratch;
  tess::RegionGraph graph;
  EXPECT_EQ(
      (tess::build_region_graph<World, PassableTag>(world, scratch, graph))
          .status,
      tess::TopologyStatus::Built);
  return graph;
}

struct ChunkGrouping {
  auto operator()(tess::RegionRef ref, const tess::LocalRegion&) const noexcept
      -> std::uint64_t {
    return ref.chunk.value + 10u;
  }
};

TEST(TessAreaIndex, GroupsRegionsAndSummarizesPortalAdjacency) {
  World world;
  fill_open(world);
  const auto graph = graph_for(world);
  tess::AreaIndexScratch scratch;
  tess::AreaIndex index;

  const auto result =
      tess::build_area_index(graph, ChunkGrouping{}, scratch, index);

  EXPECT_EQ(result.status, tess::AreaBuildStatus::Built);
  ASSERT_EQ(index.areas().size(), 2u);
  EXPECT_EQ(index.areas()[0].key, 10u);
  EXPECT_EQ(index.areas()[0].tile_count, 16u);
  EXPECT_EQ(index.areas()[0].bounds, (tess::Box3{{0, 0, 0}, {4, 4, 1}}));
  EXPECT_EQ(index.areas()[1].key, 11u);
  EXPECT_EQ(index.areas()[1].tile_count, 16u);
  ASSERT_EQ(index.connections().size(), 1u);
  EXPECT_EQ(index.connections()[0].first, tess::AreaId{1});
  EXPECT_EQ(index.connections()[0].second, tess::AreaId{2});
  EXPECT_EQ(index.connections()[0].directed_portal_count, 8u);
}

TEST(TessAreaIndex, LooksUpAreaFromRegionAndCoordinate) {
  World world;
  fill_open(world);
  const auto graph = graph_for(world);
  tess::AreaIndexScratch scratch;
  tess::AreaIndex index;
  (void)tess::build_area_index(graph, ChunkGrouping{}, scratch, index);

  EXPECT_EQ(index.area_of(graph.region_of<Shape>({0, 0, 0})), tess::AreaId{1});
  EXPECT_EQ(index.area_of<Shape>(graph, {7, 3, 0}), tess::AreaId{2});
  EXPECT_EQ(index.area_of<Shape>(graph, {-1, 0, 0}), tess::invalid_area_id);
}

TEST(TessAreaIndex, SharedCallerKeyMergesRegionsWithoutSelfAdjacency) {
  World world;
  fill_open(world);
  const auto graph = graph_for(world);
  tess::AreaIndexScratch scratch;
  tess::AreaIndex index;

  (void)tess::build_area_index(
      graph, [](tess::RegionRef, const tess::LocalRegion&) { return 42u; },
      scratch, index);

  ASSERT_EQ(index.areas().size(), 1u);
  EXPECT_EQ(index.areas()[0].tile_count, 32u);
  EXPECT_EQ(index.areas()[0].bounds, (tess::Box3{{0, 0, 0}, {8, 4, 1}}));
  EXPECT_TRUE(index.connections().empty());
}

TEST(TessAreaIndex, GraphEditInvalidatesPriorIndex) {
  World world;
  fill_open(world);
  auto graph = graph_for(world);
  tess::AreaIndexScratch area_scratch;
  tess::AreaIndex index;
  (void)tess::build_area_index(graph, ChunkGrouping{}, area_scratch, index);
  ASSERT_TRUE(index.is_valid(graph));

  world.template field<PassableTag>({0, 0, 0}) = 0;
  world.mark_dirty(tess::ChunkKey{0}, 1u, tess::Box3{{0, 0, 0}, {1, 1, 1}});
  tess::LocalTopologyScratch local_scratch;
  const auto dirty = std::array{tess::ChunkKey{0}};
  (void)tess::update_region_graph<World, PassableTag>(world, local_scratch,
                                                      graph, dirty);

  EXPECT_FALSE(index.is_valid(graph));
}

TEST(TessAreaIndex, SupportsSparseResidentRegionGraphs) {
  using Sparse = tess::SparseResidentWorld<Shape, Schema>;
  Sparse world{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
  for (const auto key : {tess::ChunkKey{0}, tess::ChunkKey{1}}) {
    world.ensure_resident(key);
    auto values = world.chunk(key).template field_span<PassableTag>();
    std::fill(values.begin(), values.end(), 1);
  }
  tess::LocalTopologyScratch local_scratch;
  tess::SparseRegionGraph graph;
  ASSERT_EQ((tess::build_region_graph<Sparse, PassableTag>(world, local_scratch,
                                                           graph))
                .status,
            tess::TopologyStatus::Built);
  tess::AreaIndexScratch area_scratch;
  tess::AreaIndex index;

  const auto result =
      tess::build_area_index(graph, ChunkGrouping{}, area_scratch, index);

  EXPECT_EQ(result.status, tess::AreaBuildStatus::Built);
  EXPECT_EQ(index.areas().size(), 2u);
  EXPECT_TRUE(index.is_valid(graph));
}

TEST(TessAreaIndex, ReservedWarmRebuildDoesNotAllocate) {
  World world;
  fill_open(world);
  const auto graph = graph_for(world);
  tess::AreaIndexScratch scratch;
  scratch.reserve(graph.region_count(), graph.portals().size());
  tess::AreaIndex index;
  index.reserve(graph.region_count(), graph.portals().size());
  (void)tess::build_area_index(graph, ChunkGrouping{}, scratch, index);

  {
    tess_test::ScopedAllocationCounter counter;
    for (int i = 0; i < 100; ++i) {
      const auto result =
          tess::build_area_index(graph, ChunkGrouping{}, scratch, index);
      EXPECT_EQ(result.status, tess::AreaBuildStatus::Built);
    }
    EXPECT_EQ(counter.count(), 0u);
  }
}

}  // namespace

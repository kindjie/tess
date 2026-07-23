#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "allocation_counter.h"

namespace {

struct TerrainTag {};
struct CostTag {};

using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint32_t>,
                                 tess::Field<CostTag, std::uint16_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

constexpr std::array<tess::ChunkKey, 2> Keys{tess::ChunkKey{0},
                                             tess::ChunkKey{1}};

void fill(World& world) {
  for (const auto key : Keys) {
    auto terrain = world.chunk(key).template field_span<TerrainTag>();
    for (std::uint64_t i = 0; i < terrain.size(); ++i) {
      terrain[i] = static_cast<std::uint32_t>(key.value * terrain.size() + i);
    }
  }
}

TEST(TessBlockPipeline, LazyFilterMapReduceMatchesReference) {
  World world;
  fill(world);
  const auto ctx = tess::block_ctx<tess::WritePolicy::ReadOnly>(
      world, tess::chunk_domain(Keys));
  tess::PipelineDiagnostics diagnostics;

  const auto sum =
      tess::block_tiles(ctx, diagnostics)
          .filter([](const auto& tile) {
            return tile.chunk.template field_span<TerrainTag>()[tile.id.value] %
                       2u ==
                   0;
          })
          .map([](const auto& tile) {
            return tile.chunk.template field_span<TerrainTag>()[tile.id.value];
          })
          .reduce(std::uint64_t{0},
                  [](std::uint64_t total, std::uint32_t value) {
                    return total + value;
                  });

  EXPECT_EQ(sum, 4032u);
  EXPECT_EQ(diagnostics.blocks_read(), 2u);
  EXPECT_EQ(diagnostics.items_read(), 128u);
  EXPECT_EQ(diagnostics.items_filtered(), 64u);
  EXPECT_EQ(diagnostics.items_emitted(), 64u);
}

TEST(TessBlockPipeline, ForEachCanMutateThroughPolicyQualifiedTiles) {
  World world;
  fill(world);
  const auto ctx = tess::block_ctx<tess::WritePolicy::UniquePerChunk>(
      world, tess::chunk_domain(Keys));

  tess::block_tiles(ctx)
      .filter([](const auto& tile) { return tile.world.x < 3; })
      .for_each([](const auto& tile) {
        tile.chunk.template field_span<CostTag>()[tile.id.value] = 9;
      });

  std::size_t changed = 0;
  for (const auto key : Keys) {
    for (const auto value : world.chunk(key).template field_span<CostTag>()) {
      changed += value == 9 ? 1u : 0u;
    }
  }
  EXPECT_EQ(changed, 24u);
}

TEST(TessBlockPipeline, FlatMapBuildsBoundedFrontierInStableOrder) {
  const std::array<std::uint32_t, 3> input{1, 4, 7};
  std::array<std::uint32_t, 5> output{};
  const auto result =
      tess::pipeline_from(std::span{input})
          .flat_map([](std::uint32_t value) {
            return std::array<std::uint32_t, 3>{value - 1, value, value + 1};
          })
          .filter([](std::uint32_t value) { return value % 2u == 0; })
          .to_frontier(output);

  EXPECT_FALSE(result.capacity_exhausted);
  EXPECT_EQ(result.written, 5u);
  EXPECT_EQ(result.required, 5u);
  EXPECT_EQ(output, (std::array<std::uint32_t, 5>{0, 2, 4, 6, 8}));
}

TEST(TessBlockPipeline, BoundedCollectionReportsRequiredCapacity) {
  const std::array<std::uint32_t, 3> input{1, 4, 7};
  std::array<std::uint32_t, 3> output{};
  const auto result =
      tess::pipeline_from(std::span{input})
          .flat_map([](std::uint32_t value) {
            return std::array<std::uint32_t, 3>{value - 1, value, value + 1};
          })
          .collect_into(output);

  EXPECT_TRUE(result.capacity_exhausted);
  EXPECT_EQ(result.written, 3u);
  EXPECT_EQ(result.required, 9u);
  EXPECT_EQ(output, (std::array<std::uint32_t, 3>{0, 1, 2}));
}

TEST(TessBlockPipeline, AllocatingTerminalIsExplicitAndMatchesFusedOutput) {
  World world;
  fill(world);
  const auto ctx = tess::block_ctx<tess::WritePolicy::ReadOnly>(
      world, tess::chunk_domain(Keys));
  tess::PipelineDiagnostics diagnostics;

  const auto values =
      tess::block_tiles(ctx, diagnostics)
          .filter([](const auto& tile) { return tile.world.y == 3; })
          .map([](const auto& tile) {
            return tile.chunk.template field_span<TerrainTag>()[tile.id.value];
          })
          .to_sequence_allocating();
  ASSERT_EQ(values.size(), 16u);
  EXPECT_EQ(values.front(), 24u);
  EXPECT_EQ(values.back(), 95u);
  EXPECT_EQ(diagnostics.materializations(), 1u);

  std::uint64_t fused_sum = 0;
  tess::block_tiles(ctx)
      .filter([](const auto& tile) { return tile.world.y == 3; })
      .map([](const auto& tile) {
        return tile.chunk.template field_span<TerrainTag>()[tile.id.value];
      })
      .for_each([&](std::uint32_t value) { fused_sum += value; });
  std::uint64_t materialized_sum = 0;
  for (const auto value : values) {
    materialized_sum += value;
  }
  EXPECT_EQ(fused_sum, materialized_sum);
}

TEST(TessBlockPipeline, FusedWarmPathDoesNotAllocate) {
  World world;
  fill(world);
  const auto ctx = tess::block_ctx<tess::WritePolicy::ReadOnly>(
      world, tess::chunk_domain(Keys));
  std::uint64_t checksum = 0;

  {
    tess_test::ScopedAllocationCounter counter;
    for (int i = 0; i < 32; ++i) {
      checksum += tess::block_tiles(ctx)
                      .filter([](const auto& tile) { return tile.local.x < 4; })
                      .map([](const auto& tile) {
                        return tile.chunk
                            .template field_span<TerrainTag>()[tile.id.value];
                      })
                      .reduce(std::uint64_t{0},
                              [](std::uint64_t total, std::uint32_t value) {
                                return total + value;
                              });
    }
    EXPECT_EQ(counter.count(), 0u);
  }
  EXPECT_GT(checksum, 0u);
}

}  // namespace

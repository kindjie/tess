#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>

#include "gpu_mock_backend.h"

namespace {

struct CostTag {};
struct HeightTag {};
struct FlagTag {};
struct InfluenceTag {};

using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<
    tess::Field<CostTag, std::uint32_t>, tess::Field<HeightTag, std::int16_t>,
    tess::Field<FlagTag, bool>, tess::Field<InfluenceTag, float>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

// Both shipped backends satisfy the concept; the concept is the
// compile-time polymorphism seam (no virtual dispatch anywhere).
static_assert(tess::gpu::GpuBackend<tess::gpu::NoGpuBackend>);
static_assert(tess::gpu::GpuBackend<tess_test::MockGpuBackend>);

TEST(TessGpuInterface, NoGpuBackendReportsNothingAndRefusesEverything) {
  tess::gpu::NoGpuBackend backend;
  EXPECT_EQ(backend.capabilities(), tess::gpu::GpuCapabilities{});
  EXPECT_FALSE(backend.upload(tess::gpu::UploadDesc{}));
  EXPECT_FALSE(backend.dispatch(tess::gpu::DispatchDesc{}));
  EXPECT_FALSE(backend.readback(tess::gpu::ReadbackDesc{}));
}

TEST(TessGpuInterface, FieldMirrorDescMatchesTheLiveWorldLayout) {
  World world;
  const auto key = tess::ChunkKey{3};

  constexpr auto cost = tess::gpu::field_mirror_desc<World, CostTag>();
  static_assert(cost.format == tess::gpu::GpuFieldFormat::U32);
  static_assert(cost.value_bytes == 4);
  const auto cost_span = world.field_span<CostTag>(key);
  EXPECT_EQ(cost.tiles_per_chunk, cost_span.size());
  EXPECT_EQ(cost.bytes_per_chunk, cost_span.size_bytes());
  EXPECT_EQ(cost.chunk_count, World::chunk_count);
  EXPECT_EQ(cost.total_bytes(), cost_span.size_bytes() * World::chunk_count);
  EXPECT_EQ(cost.field_index, 0u);

  // Signedness, bool storage, and float all map to distinct formats.
  constexpr auto height = tess::gpu::field_mirror_desc<World, HeightTag>();
  static_assert(height.format == tess::gpu::GpuFieldFormat::I16);
  static_assert(height.value_bytes == 2);
  constexpr auto flag = tess::gpu::field_mirror_desc<World, FlagTag>();
  static_assert(flag.format == tess::gpu::GpuFieldFormat::U8);
  constexpr auto influence =
      tess::gpu::field_mirror_desc<World, InfluenceTag>();
  static_assert(influence.format == tess::gpu::GpuFieldFormat::F32);
  EXPECT_EQ(height.field_index, 1u);
  EXPECT_EQ(flag.field_index, 2u);
  EXPECT_EQ(influence.field_index, 3u);

  const auto height_span = world.field_span<HeightTag>(key);
  EXPECT_EQ(height.bytes_per_chunk, height_span.size_bytes());
}

TEST(TessGpuInterface, UploadDescPointsAtTheChunkSpan) {
  World world;
  const auto key = tess::ChunkKey{5};
  world.field<CostTag>(tess::Coord3{9, 17, 0}) = 42;  // any content

  const auto upload = tess::gpu::upload_desc<CostTag>(world, key);
  const auto span = world.field_span<CostTag>(key);
  constexpr auto desc = tess::gpu::field_mirror_desc<World, CostTag>();

  EXPECT_EQ(upload.chunk_key.value, key.value);
  EXPECT_EQ(upload.field_index, desc.field_index);
  EXPECT_EQ(upload.byte_size, span.size_bytes());
  EXPECT_EQ(upload.data, static_cast<const void*>(span.data()));
  EXPECT_EQ(upload.buffer_offset, key.value * desc.bytes_per_chunk);
}

TEST(TessGpuInterface, SparseWorldsStageResidentChunks) {
  using SparseWorld = tess::SparseResidentWorld<Shape, Schema>;
  SparseWorld world{tess::ResidencyConfig{1U << 20U}};
  const auto key =
      tess::chunk_key<Shape>(tess::tile_key<Shape>(tess::Coord3{8, 8, 0}));
  (void)world.ensure_resident(key);

  const auto upload = tess::gpu::upload_desc<CostTag>(world, key);
  const auto span = world.field_span<CostTag>(key);
  EXPECT_EQ(upload.data, static_cast<const void*>(span.data()));
  EXPECT_EQ(upload.byte_size, span.size_bytes());
}

TEST(TessGpuInterface, MockRecordsUploadDispatchReadbackInOrder) {
  World world;
  tess_test::MockGpuBackend backend;

  // The steady-state shape: dirty-chunk uploads, one dispatch over the
  // product, a summary readback -- never a full-field readback.
  ASSERT_TRUE(backend.upload(
      tess::gpu::upload_desc<CostTag>(world, tess::ChunkKey{1})));
  ASSERT_TRUE(backend.upload(
      tess::gpu::upload_desc<CostTag>(world, tess::ChunkKey{7})));
  ASSERT_TRUE(backend.dispatch(tess::gpu::DispatchDesc{
      .product_key = 99, .input_field_index = 0, .chunk_count = 2}));
  ASSERT_TRUE(backend.readback(
      tess::gpu::ReadbackDesc{.product_key = 99,
                              .policy = tess::gpu::ReadbackPolicy::Summary,
                              .byte_size = 64}));

  const auto& calls = backend.calls();
  ASSERT_EQ(calls.size(), 4u);
  EXPECT_EQ(calls[0].kind, tess_test::GpuCallKind::Upload);
  EXPECT_EQ(calls[0].key, 1u);
  EXPECT_EQ(calls[1].kind, tess_test::GpuCallKind::Upload);
  EXPECT_EQ(calls[1].key, 7u);
  EXPECT_EQ(calls[2].kind, tess_test::GpuCallKind::Dispatch);
  EXPECT_EQ(calls[2].key, 99u);
  EXPECT_EQ(calls[2].bytes, 2u);
  EXPECT_EQ(calls[3].kind, tess_test::GpuCallKind::Readback);
  EXPECT_EQ(calls[3].field_index,
            static_cast<std::uint32_t>(tess::gpu::ReadbackPolicy::Summary));
}

TEST(TessGpuInterface, MockRefusesBeyondItsCapabilities) {
  // No compute: everything is refused and nothing is recorded, exactly
  // the planner's never-choose-GPU signal.
  tess_test::MockGpuBackend inert{tess::gpu::GpuCapabilities{}};
  World world;
  EXPECT_FALSE(
      inert.upload(tess::gpu::upload_desc<CostTag>(world, tess::ChunkKey{0})));
  EXPECT_FALSE(inert.dispatch(tess::gpu::DispatchDesc{}));
  EXPECT_TRUE(inert.calls().empty());

  // Oversized requests are refused per capability limits.
  tess_test::MockGpuBackend tiny{tess::gpu::GpuCapabilities{
      .compute = true, .max_buffer_bytes = 16, .max_dispatch_chunks = 1}};
  EXPECT_FALSE(
      tiny.upload(tess::gpu::upload_desc<CostTag>(world, tess::ChunkKey{2})));
  EXPECT_FALSE(tiny.dispatch(tess::gpu::DispatchDesc{.chunk_count = 4}));
  // An explicit None readback is a refusal, not a silent no-op.
  EXPECT_FALSE(tiny.readback(
      tess::gpu::ReadbackDesc{.policy = tess::gpu::ReadbackPolicy::None}));
  EXPECT_TRUE(tiny.calls().empty());
}

}  // namespace

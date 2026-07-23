// The optional backend requires the stable C header to be included first.
// clang-format off
#include <webgpu/webgpu.h>
// clang-format on

#include <gtest/gtest.h>
#include <tess/gpu/webgpu_backend.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

// The stable WebGPU C API exposes reference-counted pointer handles. Owning
// them in tests ensures fatal assertions release the caller's reference while
// the backend independently exercises its retain/release contract.
struct DeviceRelease {
  void operator()(WGPUDevice device) const noexcept {
    wgpuDeviceRelease(device);
  }
};

struct PipelineRelease {
  void operator()(WGPUComputePipeline pipeline) const noexcept {
    wgpuComputePipelineRelease(pipeline);
  }
};

struct BindGroupRelease {
  void operator()(WGPUBindGroup bind_group) const noexcept {
    wgpuBindGroupRelease(bind_group);
  }
};

using DeviceOwner = std::unique_ptr<WGPUDeviceImpl, DeviceRelease>;
using PipelineOwner = std::unique_ptr<WGPUComputePipelineImpl, PipelineRelease>;
using BindGroupOwner = std::unique_ptr<WGPUBindGroupImpl, BindGroupRelease>;

struct CostTag {};
using Shape = tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{4, 4, 1}>;
using Schema = tess::FieldSchema<tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

struct ReadbackCapture {
  tess::gpu::WebGpuReadbackStatus status =
      tess::gpu::WebGpuReadbackStatus::Failed;
  tess::gpu::GpuProductHandle handle{};
  std::array<std::uint32_t, 4> values{};
  std::size_t calls = 0;
};

void capture_readback(tess::gpu::GpuProductHandle handle,
                      tess::gpu::WebGpuReadbackStatus status, const void* data,
                      std::size_t size, void* userdata) noexcept {
  auto& capture = *static_cast<ReadbackCapture*>(userdata);
  capture.status = status;
  capture.handle = handle;
  ++capture.calls;
  if (data != nullptr && size == sizeof(capture.values)) {
    std::memcpy(capture.values.data(), data, size);
  }
}

[[nodiscard]] auto make_backend(WGPUDevice device) -> tess::gpu::WebGpuBackend {
  return tess::gpu::WebGpuBackend{device,
                                  tess::gpu::WebGpuBackendConfig{
                                      .max_buffer_bytes = 1u << 20u,
                                      .max_dispatch_chunks = 1024,
                                      .max_inflight_readback_bytes = 4096,
                                      .field_capacity = 2,
                                      .product_capacity = 2,
                                  }};
}

TEST(TessWebGpuBackend, RegistersMirrorsAndUploadsChunkBytes) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto backend = make_backend(device.get());
  device.reset();
  World world;
  const auto key = tess::ChunkKey{2};
  world.field_span<CostTag>(key)[0] = 42;

  ASSERT_TRUE(
      backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
  ASSERT_NE(backend.field_buffer(0), nullptr);
  EXPECT_TRUE(backend.upload(tess::gpu::upload_desc<CostTag>(world, key)));
  EXPECT_EQ(backend.field_buffer(0)->bytes[key.value * 64], std::byte{42});
}

TEST(TessWebGpuBackend, DispatchRequiresCurrentGenerationHandle) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto backend = make_backend(device.get());
  device.reset();
  PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
  BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};

  const auto first = backend.register_product(tess::gpu::WebGpuProductDesc{
      .product_key = 17,
      .input_field_index = 0,
      .pipeline = pipeline.get(),
      .bind_group = bind_group.get(),
  });
  ASSERT_TRUE(first.has_value());
  const auto first_handle = first.value_or(tess::gpu::GpuProductHandle{});
  EXPECT_TRUE(backend.dispatch(tess::gpu::DispatchDesc{
      .product_key = first_handle.key,
      .product_generation = first_handle.generation,
      .input_field_index = 0,
      .chunk_count = 3,
      .workgroups_per_chunk = 2,
  }));
  EXPECT_EQ(tess_webgpu_stub::dispatched_x, 6u);

  ASSERT_TRUE(backend.unregister_product(first_handle));
  EXPECT_FALSE(backend.valid(first_handle));
  const auto second = backend.register_product(tess::gpu::WebGpuProductDesc{
      .product_key = 17,
      .input_field_index = 0,
      .pipeline = pipeline.get(),
      .bind_group = bind_group.get(),
  });
  ASSERT_TRUE(second.has_value());
  const auto second_handle = second.value_or(tess::gpu::GpuProductHandle{});
  EXPECT_NE(second_handle.generation, first_handle.generation);
  EXPECT_FALSE(backend.dispatch(tess::gpu::DispatchDesc{
      .product_key = first_handle.key,
      .product_generation = first_handle.generation,
      .input_field_index = 0,
      .chunk_count = 1,
  }));
}

TEST(TessWebGpuBackend, ReadbackCompletesAsynchronouslyAfterDestruction) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  ReadbackCapture capture;
  tess::gpu::GpuProductHandle handle;
  {
    auto backend = make_backend(device.get());
    device.reset();
    World world;
    ASSERT_TRUE(
        backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
    std::array<std::uint32_t, 4> expected{2, 4, 6, 8};
    const auto upload = tess::gpu::UploadDesc{
        .field_index = 0,
        .buffer_offset = 0,
        .byte_size = sizeof(expected),
        .data = expected.data(),
    };
    ASSERT_TRUE(backend.upload(upload));

    PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
    BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};
    const auto registered =
        backend.register_product(tess::gpu::WebGpuProductDesc{
            .product_key = 99,
            .input_field_index = 0,
            .pipeline = pipeline.get(),
            .bind_group = bind_group.get(),
            .readback_source = backend.field_buffer(0),
            .readback_byte_size = sizeof(expected),
            .readback_callback = capture_readback,
            .readback_userdata = &capture,
        });
    ASSERT_TRUE(registered.has_value());
    handle = registered.value_or(tess::gpu::GpuProductHandle{});
    ASSERT_TRUE(backend.readback(tess::gpu::ReadbackDesc{
        .product_key = handle.key,
        .product_generation = handle.generation,
        .policy = tess::gpu::ReadbackPolicy::Summary,
        .byte_size = sizeof(expected),
    }));
    EXPECT_EQ(capture.calls, 0u);
  }

  tess_webgpu_stub::complete_map(true);
  EXPECT_EQ(capture.calls, 1u);
  EXPECT_EQ(capture.status, tess::gpu::WebGpuReadbackStatus::Complete);
  EXPECT_EQ(capture.handle, handle);
  EXPECT_EQ(capture.values, (std::array<std::uint32_t, 4>{2, 4, 6, 8}));
}

TEST(TessWebGpuBackend, RefusesInvalidWorkAndDeviceLoss) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto backend = make_backend(device.get());
  device.reset();

  EXPECT_FALSE(backend.upload(tess::gpu::UploadDesc{}));
  EXPECT_FALSE(backend.dispatch(tess::gpu::DispatchDesc{}));
  EXPECT_FALSE(backend.readback(tess::gpu::ReadbackDesc{}));
  backend.notify_device_lost();
  EXPECT_FALSE(backend.capabilities().compute);
  EXPECT_FALSE(
      backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
}

TEST(TessWebGpuBackend, DisabledConfigDoesNotConsumeBorrowedDevice) {
  DeviceOwner device{tess_webgpu_stub::make_device()};
  ASSERT_EQ(device->refs, 1u);
  {
    tess::gpu::WebGpuBackend backend{
        device.get(), tess::gpu::WebGpuBackendConfig{.max_buffer_bytes = 0}};
    EXPECT_FALSE(backend.capabilities().compute);
    EXPECT_EQ(device->refs, 1u);
  }
  EXPECT_EQ(device->refs, 1u);
}

static_assert(tess::gpu::GpuBackend<tess::gpu::WebGpuBackend>);

}  // namespace

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
#include <limits>
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

struct BufferRelease {
  void operator()(WGPUBuffer buffer) const noexcept {
    wgpuBufferRelease(buffer);
  }
};

using DeviceOwner = std::unique_ptr<WGPUDeviceImpl, DeviceRelease>;
using PipelineOwner = std::unique_ptr<WGPUComputePipelineImpl, PipelineRelease>;
using BindGroupOwner = std::unique_ptr<WGPUBindGroupImpl, BindGroupRelease>;
using BufferOwner = std::unique_ptr<WGPUBufferImpl, BufferRelease>;

[[nodiscard]] auto make_readback_source(WGPUDevice device, const void* data,
                                        std::size_t size) -> BufferOwner {
  auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
  desc.size = size;
  BufferOwner buffer{wgpuDeviceCreateBuffer(device, &desc)};
  if (buffer != nullptr && data != nullptr) {
    std::memcpy(buffer->bytes.data(), data, size);
  }
  return buffer;
}

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

TEST(TessWebGpuBackend, RefusesOverflowingMirrorDescriptions) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto backend = make_backend(device.get());
  device.reset();

  const auto desc = tess::gpu::FieldMirrorDesc{
      .field_index = 3,
      .value_bytes = 4,
      .tiles_per_chunk = 1,
      .bytes_per_chunk = std::numeric_limits<std::uint64_t>::max(),
      .chunk_count = 2,
  };
  EXPECT_FALSE(desc.total_bytes_fits());
  EXPECT_EQ(desc.total_bytes(), std::numeric_limits<std::uint64_t>::max());
  EXPECT_FALSE(backend.register_field(desc));
}

TEST(TessWebGpuBackend, ReadbackSourceMustHaveCopySourceUsage) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto backend = make_backend(device.get());
  device.reset();
  ASSERT_TRUE(
      backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
  PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
  BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};

  EXPECT_FALSE(
      backend
          .register_product(tess::gpu::WebGpuProductDesc{
              .product_key = 99,
              .input_field_index = 0,
              .pipeline = pipeline.get(),
              .bind_group = bind_group.get(),
              // Field mirrors are copy destinations, not readback sources.
              .readback_source = backend.field_buffer(0),
              .readback_byte_size = 16,
              .readback_callback = capture_readback,
          })
          .has_value());
}

TEST(TessWebGpuBackend, NoReadbackSourceRejectsOrphanedMetadata) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  tess::gpu::WebGpuBackend backend{device.get(),
                                   tess::gpu::WebGpuBackendConfig{
                                       .max_buffer_bytes = 1u << 20u,
                                       .max_dispatch_chunks = 1024,
                                       .max_inflight_readback_bytes = 4096,
                                       .field_capacity = 2,
                                       .product_capacity = 4,
                                   }};
  device.reset();
  PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
  BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};
  int userdata = 0;

  EXPECT_FALSE(backend
                   .register_product(tess::gpu::WebGpuProductDesc{
                       .product_key = 91,
                       .pipeline = pipeline.get(),
                       .bind_group = bind_group.get(),
                       .readback_source_offset = 4,
                   })
                   .has_value());
  EXPECT_FALSE(backend
                   .register_product(tess::gpu::WebGpuProductDesc{
                       .product_key = 92,
                       .pipeline = pipeline.get(),
                       .bind_group = bind_group.get(),
                       .readback_callback = capture_readback,
                   })
                   .has_value());
  EXPECT_FALSE(backend
                   .register_product(tess::gpu::WebGpuProductDesc{
                       .product_key = 93,
                       .pipeline = pipeline.get(),
                       .bind_group = bind_group.get(),
                       .readback_userdata = &userdata,
                   })
                   .has_value());
}

TEST(TessWebGpuStubDeathTest, QueueWriteRejectsOutOfBoundsCopy) {
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  desc.size = 4;
  BufferOwner buffer{wgpuDeviceCreateBuffer(device.get(), &desc)};
  const std::array<std::byte, 4> source{};

  EXPECT_DEATH(wgpuQueueWriteBuffer(nullptr, buffer.get(), 2, source.data(),
                                    source.size()),
               "");
}

TEST(TessWebGpuStubDeathTest, EncoderRejectsOutOfBoundsCopy) {
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  desc.size = 4;
  BufferOwner source{wgpuDeviceCreateBuffer(device.get(), &desc)};
  BufferOwner destination{wgpuDeviceCreateBuffer(device.get(), &desc)};

  EXPECT_DEATH(wgpuCommandEncoderCopyBufferToBuffer(nullptr, source.get(), 2,
                                                    destination.get(), 0, 4),
               "");
}

TEST(TessWebGpuBackend, SizeConversionRejectsNarrowing) {
  constexpr auto uint32_max = std::numeric_limits<std::uint32_t>::max();
  static_assert(tess::gpu::detail::fits_size<std::uint32_t>(uint32_max));
  static_assert(!tess::gpu::detail::fits_size<std::uint32_t>(
      static_cast<std::uint64_t>(uint32_max) + 1));
  static_assert(tess::gpu::detail::fits_size<std::size_t>(
      std::numeric_limits<std::size_t>::max()));
}

TEST(TessWebGpuBackend, DispatchRequiresRegisteredFieldAndCurrentGeneration) {
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
  EXPECT_FALSE(backend.dispatch(tess::gpu::DispatchDesc{
      .product_key = first_handle.key,
      .product_generation = first_handle.generation,
      .input_field_index = 0,
      .chunk_count = 3,
      .workgroups_per_chunk = 2,
  }));
  ASSERT_TRUE(
      backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
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

TEST(TessWebGpuBackend, RefusesDispatchBeyondWorkgroupXLimit) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto backend = make_backend(device.get());
  device.reset();
  World world;
  ASSERT_TRUE(
      backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
  PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
  BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};
  const auto registered = backend.register_product(tess::gpu::WebGpuProductDesc{
      .product_key = 17,
      .input_field_index = 0,
      .pipeline = pipeline.get(),
      .bind_group = bind_group.get(),
  });
  ASSERT_TRUE(registered.has_value());
  const auto handle = registered.value_or(tess::gpu::GpuProductHandle{});

  EXPECT_FALSE(backend.dispatch(tess::gpu::DispatchDesc{
      .product_key = handle.key,
      .product_generation = handle.generation,
      .input_field_index = 0,
      .chunk_count = 513,
      .workgroups_per_chunk = 128,
  }));
  EXPECT_EQ(tess_webgpu_stub::dispatched_x, 0u);
}

TEST(TessWebGpuBackend, ReadbackCompletesAsynchronouslyAfterDestruction) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  ReadbackCapture capture;
  tess::gpu::GpuProductHandle handle;
  {
    auto backend = make_backend(device.get());
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
    auto source =
        make_readback_source(device.get(), expected.data(), sizeof(expected));
    ASSERT_NE(source, nullptr);
    device.reset();

    PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
    BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};
    const auto registered =
        backend.register_product(tess::gpu::WebGpuProductDesc{
            .product_key = 99,
            .input_field_index = 0,
            .pipeline = pipeline.get(),
            .bind_group = bind_group.get(),
            .readback_source = source.get(),
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

TEST(TessWebGpuBackend, NullMapFutureRejectsAndReleasesReadbackBudget) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  ReadbackCapture capture;
  tess::gpu::WebGpuBackend backend{
      device.get(), tess::gpu::WebGpuBackendConfig{
                        .max_buffer_bytes = 1u << 20u,
                        .max_dispatch_chunks = 1024,
                        .max_inflight_readback_bytes = sizeof(capture.values),
                        .field_capacity = 2,
                        .product_capacity = 2,
                    }};
  auto source =
      make_readback_source(device.get(), nullptr, sizeof(capture.values));
  ASSERT_NE(source, nullptr);
  device.reset();
  World world;
  ASSERT_TRUE(
      backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
  PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
  BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};
  const auto registered = backend.register_product(tess::gpu::WebGpuProductDesc{
      .product_key = 99,
      .input_field_index = 0,
      .pipeline = pipeline.get(),
      .bind_group = bind_group.get(),
      .readback_source = source.get(),
      .readback_byte_size = sizeof(capture.values),
      .readback_callback = capture_readback,
      .readback_userdata = &capture,
  });
  ASSERT_TRUE(registered.has_value());
  const auto handle = registered.value_or(tess::gpu::GpuProductHandle{});
  const auto request = tess::gpu::ReadbackDesc{
      .product_key = handle.key,
      .product_generation = handle.generation,
      .policy = tess::gpu::ReadbackPolicy::Summary,
      .byte_size = sizeof(capture.values),
  };

  tess_webgpu_stub::map_future_id = 0;
  EXPECT_FALSE(backend.readback(request));
  EXPECT_EQ(capture.calls, 0u);
  EXPECT_TRUE(tess_webgpu_stub::pending_maps.empty());

  tess_webgpu_stub::map_future_id = 1;
  EXPECT_TRUE(backend.readback(request));
  tess_webgpu_stub::complete_map(true);
  EXPECT_EQ(capture.calls, 1u);
}

TEST(TessWebGpuBackend, InlineMapCompletionDoesNotTouchFreedOperation) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto backend = make_backend(device.get());
  ReadbackCapture capture;
  auto source =
      make_readback_source(device.get(), nullptr, sizeof(capture.values));
  ASSERT_NE(source, nullptr);
  device.reset();
  World world;
  ASSERT_TRUE(
      backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
  PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
  BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};
  const auto registered = backend.register_product(tess::gpu::WebGpuProductDesc{
      .product_key = 99,
      .input_field_index = 0,
      .pipeline = pipeline.get(),
      .bind_group = bind_group.get(),
      .readback_source = source.get(),
      .readback_byte_size = sizeof(capture.values),
      .readback_callback = capture_readback,
      .readback_userdata = &capture,
  });
  ASSERT_TRUE(registered.has_value());
  const auto handle = registered.value_or(tess::gpu::GpuProductHandle{});
  const auto request = tess::gpu::ReadbackDesc{
      .product_key = handle.key,
      .product_generation = handle.generation,
      .policy = tess::gpu::ReadbackPolicy::Summary,
      .byte_size = sizeof(capture.values),
  };

  tess_webgpu_stub::complete_map_inline = true;
  EXPECT_TRUE(backend.readback(request));
  EXPECT_EQ(capture.calls, 1u);
  EXPECT_EQ(capture.status, tess::gpu::WebGpuReadbackStatus::Complete);
  EXPECT_TRUE(tess_webgpu_stub::pending_maps.empty());
  EXPECT_TRUE(backend.readback(request));
  EXPECT_EQ(capture.calls, 2u);
}

TEST(TessWebGpuBackend, OverlappingReadbacksShareBudgetAndReleaseOnFailure) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  ReadbackCapture capture;
  tess::gpu::WebGpuBackend backend{
      device.get(),
      tess::gpu::WebGpuBackendConfig{
          .max_buffer_bytes = 1u << 20u,
          .max_dispatch_chunks = 1024,
          .max_inflight_readback_bytes = 2 * sizeof(capture.values),
          .field_capacity = 2,
          .product_capacity = 2,
      }};
  auto source =
      make_readback_source(device.get(), nullptr, sizeof(capture.values));
  ASSERT_NE(source, nullptr);
  device.reset();
  World world;
  ASSERT_TRUE(
      backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
  PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
  BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};
  const auto registered = backend.register_product(tess::gpu::WebGpuProductDesc{
      .product_key = 99,
      .input_field_index = 0,
      .pipeline = pipeline.get(),
      .bind_group = bind_group.get(),
      .readback_source = source.get(),
      .readback_byte_size = sizeof(capture.values),
      .readback_callback = capture_readback,
      .readback_userdata = &capture,
  });
  ASSERT_TRUE(registered.has_value());
  const auto handle = registered.value_or(tess::gpu::GpuProductHandle{});
  const auto request = tess::gpu::ReadbackDesc{
      .product_key = handle.key,
      .product_generation = handle.generation,
      .policy = tess::gpu::ReadbackPolicy::Summary,
      .byte_size = sizeof(capture.values),
  };

  EXPECT_TRUE(backend.readback(request));
  EXPECT_TRUE(backend.readback(request));
  EXPECT_FALSE(backend.readback(request));
  ASSERT_EQ(tess_webgpu_stub::pending_maps.size(), 2u);

  tess_webgpu_stub::complete_map(1, false);
  EXPECT_EQ(capture.calls, 1u);
  EXPECT_EQ(capture.status, tess::gpu::WebGpuReadbackStatus::Failed);

  // The failed callback returned one reservation while the first map remains
  // pending, so exactly one more request fits.
  EXPECT_TRUE(backend.readback(request));
  EXPECT_FALSE(backend.readback(request));
  ASSERT_EQ(tess_webgpu_stub::pending_maps.size(), 2u);

  tess_webgpu_stub::complete_map(0, true);
  tess_webgpu_stub::complete_map(0, true);
  EXPECT_EQ(capture.calls, 3u);
  EXPECT_EQ(capture.status, tess::gpu::WebGpuReadbackStatus::Complete);
}

TEST(TessWebGpuBackend, ReportedDeviceErrorFailsPendingAndFutureWork) {
  tess_webgpu_stub::reset();
  DeviceOwner device{tess_webgpu_stub::make_device()};
  auto backend = make_backend(device.get());
  ReadbackCapture capture;
  auto source =
      make_readback_source(device.get(), nullptr, sizeof(capture.values));
  ASSERT_NE(source, nullptr);
  device.reset();
  World world;
  ASSERT_TRUE(
      backend.register_field(tess::gpu::field_mirror_desc<World, CostTag>()));
  PipelineOwner pipeline{tess_webgpu_stub::make_pipeline()};
  BindGroupOwner bind_group{tess_webgpu_stub::make_bind_group()};
  const auto registered = backend.register_product(tess::gpu::WebGpuProductDesc{
      .product_key = 99,
      .input_field_index = 0,
      .pipeline = pipeline.get(),
      .bind_group = bind_group.get(),
      .readback_source = source.get(),
      .readback_byte_size = sizeof(capture.values),
      .readback_callback = capture_readback,
      .readback_userdata = &capture,
  });
  ASSERT_TRUE(registered.has_value());
  const auto handle = registered.value_or(tess::gpu::GpuProductHandle{});
  const auto request = tess::gpu::ReadbackDesc{
      .product_key = handle.key,
      .product_generation = handle.generation,
      .policy = tess::gpu::ReadbackPolicy::Summary,
      .byte_size = sizeof(capture.values),
  };
  const auto upload = tess::gpu::UploadDesc{
      .field_index = 0,
      .buffer_offset = 0,
      .byte_size = sizeof(capture.values),
      .data = capture.values.data(),
  };
  const auto dispatch = tess::gpu::DispatchDesc{
      .product_key = handle.key,
      .product_generation = handle.generation,
      .input_field_index = 0,
      .chunk_count = 1,
  };
  ASSERT_TRUE(backend.upload(upload));
  ASSERT_TRUE(backend.dispatch(dispatch));
  ASSERT_TRUE(backend.readback(request));

  backend.notify_device_error();
  EXPECT_FALSE(backend.capabilities().compute);
  EXPECT_FALSE(backend.upload(upload));
  EXPECT_FALSE(backend.dispatch(dispatch));
  EXPECT_FALSE(backend.readback(request));
  tess_webgpu_stub::complete_map(0, true);
  EXPECT_EQ(capture.calls, 1u);
  EXPECT_EQ(capture.status, tess::gpu::WebGpuReadbackStatus::Failed);
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

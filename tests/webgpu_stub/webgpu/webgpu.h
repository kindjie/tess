#pragma once

// C++ test double for the stable subset of Dawn's webgpu.h used by tess.
#define WEBGPU_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

struct WGPUDeviceImpl;
struct WGPUQueueImpl;
struct WGPUBufferImpl;
struct WGPUComputePipelineImpl;
struct WGPUBindGroupImpl;
struct WGPUCommandEncoderImpl;
struct WGPUComputePassEncoderImpl;
struct WGPUCommandBufferImpl;

using WGPUDevice = WGPUDeviceImpl*;
using WGPUQueue = WGPUQueueImpl*;
using WGPUBuffer = WGPUBufferImpl*;
using WGPUComputePipeline = WGPUComputePipelineImpl*;
using WGPUBindGroup = WGPUBindGroupImpl*;
using WGPUCommandEncoder = WGPUCommandEncoderImpl*;
using WGPUComputePassEncoder = WGPUComputePassEncoderImpl*;
using WGPUCommandBuffer = WGPUCommandBufferImpl*;
using WGPUBufferUsage = std::uint64_t;
using WGPUMapMode = std::uint64_t;

inline constexpr WGPUBufferUsage WGPUBufferUsage_MapRead = 1u << 0u;
inline constexpr WGPUBufferUsage WGPUBufferUsage_CopySrc = 1u << 2u;
inline constexpr WGPUBufferUsage WGPUBufferUsage_CopyDst = 1u << 3u;
inline constexpr WGPUBufferUsage WGPUBufferUsage_Storage = 1u << 7u;
inline constexpr WGPUMapMode WGPUMapMode_Read = 1u << 0u;

enum WGPUCallbackMode : std::uint32_t {
  WGPUCallbackMode_AllowSpontaneous = 3,
};

enum WGPUMapAsyncStatus : std::uint32_t {
  WGPUMapAsyncStatus_Success = 1,
  WGPUMapAsyncStatus_Error = 2,
};

struct WGPUStringView {
  const char* data = nullptr;
  std::size_t length = 0;
};

using WGPUBufferMapCallback = void (*)(WGPUMapAsyncStatus, WGPUStringView,
                                       void*, void*);

struct WGPUBufferMapCallbackInfo {
  void* nextInChain = nullptr;
  WGPUCallbackMode mode = WGPUCallbackMode_AllowSpontaneous;
  WGPUBufferMapCallback callback = nullptr;
  void* userdata1 = nullptr;
  void* userdata2 = nullptr;
};

struct WGPUBufferDescriptor {
  void* nextInChain = nullptr;
  WGPUStringView label{};
  WGPUBufferUsage usage = 0;
  std::uint64_t size = 0;
  bool mappedAtCreation = false;
};

struct WGPUCommandEncoderDescriptor {
  void* nextInChain = nullptr;
  WGPUStringView label{};
};

struct WGPUComputePassDescriptor {
  void* nextInChain = nullptr;
  WGPUStringView label{};
  const void* timestampWrites = nullptr;
};

struct WGPUCommandBufferDescriptor {
  void* nextInChain = nullptr;
  WGPUStringView label{};
};

struct WGPUFuture {
  std::uint64_t id = 0;
};

#define WGPU_BUFFER_DESCRIPTOR_INIT \
  WGPUBufferDescriptor {}
#define WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT \
  WGPUCommandEncoderDescriptor {}
#define WGPU_COMPUTE_PASS_DESCRIPTOR_INIT \
  WGPUComputePassDescriptor {}
#define WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT \
  WGPUCommandBufferDescriptor {}
#define WGPU_BUFFER_MAP_CALLBACK_INFO_INIT \
  WGPUBufferMapCallbackInfo {}

struct WGPUDeviceImpl {
  std::size_t refs = 1;
};
struct WGPUQueueImpl {
  std::size_t refs = 1;
};
struct WGPUBufferImpl {
  std::size_t refs = 1;
  WGPUBufferUsage usage = 0;
  std::vector<std::byte> bytes;
};
struct WGPUComputePipelineImpl {
  std::size_t refs = 1;
};
struct WGPUBindGroupImpl {
  std::size_t refs = 1;
};
struct WGPUCommandEncoderImpl {};
struct WGPUComputePassEncoderImpl {};
struct WGPUCommandBufferImpl {};

namespace tess_webgpu_stub {

enum class Event : std::uint8_t {
  CreateBuffer,
  WriteBuffer,
  BeginPass,
  SetPipeline,
  SetBindGroup,
  Dispatch,
  Submit,
  CopyBuffer,
  MapAsync,
};

inline WGPUQueueImpl queue;
inline std::vector<Event> events;
inline std::uint32_t dispatched_x = 0;
inline WGPUBuffer pending_buffer = nullptr;
inline WGPUBufferMapCallbackInfo pending_callback{};

inline void reset() {
  events.clear();
  dispatched_x = 0;
  pending_buffer = nullptr;
  pending_callback = {};
}

inline auto make_device() -> WGPUDevice { return new WGPUDeviceImpl{}; }
inline auto make_pipeline() -> WGPUComputePipeline {
  return new WGPUComputePipelineImpl{};
}
inline auto make_bind_group() -> WGPUBindGroup {
  return new WGPUBindGroupImpl{};
}

inline void complete_map(bool success) {
  auto callback = pending_callback;
  pending_callback = {};
  pending_buffer = nullptr;
  callback.callback(
      success ? WGPUMapAsyncStatus_Success : WGPUMapAsyncStatus_Error, {},
      callback.userdata1, callback.userdata2);
}

}  // namespace tess_webgpu_stub

inline void wgpuDeviceAddRef(WGPUDevice device) { ++device->refs; }
inline void wgpuDeviceRelease(WGPUDevice device) {
  if (--device->refs == 0) {
    delete device;
  }
}
inline WGPUQueue wgpuDeviceGetQueue(WGPUDevice) {
  ++tess_webgpu_stub::queue.refs;
  return &tess_webgpu_stub::queue;
}
inline void wgpuQueueRelease(WGPUQueue queue) { --queue->refs; }
inline WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice,
                                         const WGPUBufferDescriptor* desc) {
  tess_webgpu_stub::events.push_back(tess_webgpu_stub::Event::CreateBuffer);
  auto* buffer = new WGPUBufferImpl{};
  buffer->usage = desc->usage;
  buffer->bytes.resize(static_cast<std::size_t>(desc->size));
  return buffer;
}
inline void wgpuBufferAddRef(WGPUBuffer buffer) { ++buffer->refs; }
inline std::uint64_t wgpuBufferGetSize(WGPUBuffer buffer) {
  return buffer->bytes.size();
}
inline void wgpuBufferRelease(WGPUBuffer buffer) {
  if (--buffer->refs == 0) {
    delete buffer;
  }
}
inline void wgpuQueueWriteBuffer(WGPUQueue, WGPUBuffer buffer,
                                 std::uint64_t offset, const void* data,
                                 std::size_t size) {
  tess_webgpu_stub::events.push_back(tess_webgpu_stub::Event::WriteBuffer);
  std::memcpy(buffer->bytes.data() + offset, data, size);
}
inline void wgpuComputePipelineAddRef(WGPUComputePipeline pipeline) {
  ++pipeline->refs;
}
inline void wgpuComputePipelineRelease(WGPUComputePipeline pipeline) {
  if (--pipeline->refs == 0) {
    delete pipeline;
  }
}
inline void wgpuBindGroupAddRef(WGPUBindGroup group) { ++group->refs; }
inline void wgpuBindGroupRelease(WGPUBindGroup group) {
  if (--group->refs == 0) {
    delete group;
  }
}
inline WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(
    WGPUDevice, const WGPUCommandEncoderDescriptor*) {
  return new WGPUCommandEncoderImpl{};
}
inline WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(
    WGPUCommandEncoder, const WGPUComputePassDescriptor*) {
  tess_webgpu_stub::events.push_back(tess_webgpu_stub::Event::BeginPass);
  return new WGPUComputePassEncoderImpl{};
}
inline void wgpuComputePassEncoderSetPipeline(WGPUComputePassEncoder,
                                              WGPUComputePipeline) {
  tess_webgpu_stub::events.push_back(tess_webgpu_stub::Event::SetPipeline);
}
inline void wgpuComputePassEncoderSetBindGroup(WGPUComputePassEncoder,
                                               std::uint32_t, WGPUBindGroup,
                                               std::size_t,
                                               const std::uint32_t*) {
  tess_webgpu_stub::events.push_back(tess_webgpu_stub::Event::SetBindGroup);
}
inline void wgpuComputePassEncoderDispatchWorkgroups(WGPUComputePassEncoder,
                                                     std::uint32_t x,
                                                     std::uint32_t,
                                                     std::uint32_t) {
  tess_webgpu_stub::events.push_back(tess_webgpu_stub::Event::Dispatch);
  tess_webgpu_stub::dispatched_x = x;
}
inline void wgpuComputePassEncoderEnd(WGPUComputePassEncoder) {}
inline void wgpuComputePassEncoderRelease(WGPUComputePassEncoder pass) {
  delete pass;
}
inline WGPUCommandBuffer wgpuCommandEncoderFinish(
    WGPUCommandEncoder, const WGPUCommandBufferDescriptor*) {
  return new WGPUCommandBufferImpl{};
}
inline void wgpuCommandEncoderRelease(WGPUCommandEncoder encoder) {
  delete encoder;
}
inline void wgpuCommandBufferRelease(WGPUCommandBuffer buffer) {
  delete buffer;
}
inline void wgpuQueueSubmit(WGPUQueue, std::size_t, const WGPUCommandBuffer*) {
  tess_webgpu_stub::events.push_back(tess_webgpu_stub::Event::Submit);
}
inline void wgpuCommandEncoderCopyBufferToBuffer(
    WGPUCommandEncoder, WGPUBuffer source, std::uint64_t source_offset,
    WGPUBuffer destination, std::uint64_t destination_offset,
    std::uint64_t size) {
  tess_webgpu_stub::events.push_back(tess_webgpu_stub::Event::CopyBuffer);
  std::memcpy(destination->bytes.data() + destination_offset,
              source->bytes.data() + source_offset,
              static_cast<std::size_t>(size));
}
inline WGPUFuture wgpuBufferMapAsync(WGPUBuffer buffer, WGPUMapMode,
                                     std::size_t, std::size_t,
                                     WGPUBufferMapCallbackInfo callback) {
  tess_webgpu_stub::events.push_back(tess_webgpu_stub::Event::MapAsync);
  tess_webgpu_stub::pending_buffer = buffer;
  tess_webgpu_stub::pending_callback = callback;
  return WGPUFuture{1};
}
inline const void* wgpuBufferGetConstMappedRange(WGPUBuffer buffer,
                                                 std::size_t offset,
                                                 std::size_t) {
  return buffer->bytes.data() + offset;
}
inline void wgpuBufferUnmap(WGPUBuffer) {}

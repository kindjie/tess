#include <webgpu/webgpu.h>

#define TESS_ENABLE_WEBGPU
#include <emscripten/emscripten.h>
#include <tess/gpu/webgpu_backend.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

constexpr const char* kShader = R"(
@group(0) @binding(0) var<storage, read> input_values: array<u32>;
@group(0) @binding(1) var<storage, read_write> output_values: array<u32>;

@compute @workgroup_size(4)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  if (id.x < 4u) {
    output_values[id.x] = input_values[id.x] * 2u;
  }
}
)";

constexpr int kPending = 0;
constexpr int kReady = 1;
constexpr int kRequestingDevice = 2;
constexpr int kRunningCompute = 3;
constexpr int kAwaitingReadback = 4;
constexpr int kAdapterUnavailable = -1;
constexpr int kFieldRegistrationFailed = -2;
constexpr int kShaderCreationFailed = -3;
constexpr int kPipelineCreationFailed = -4;
constexpr int kOutputSetupFailed = -5;
constexpr int kBindGroupCreationFailed = -6;
constexpr int kProductRegistrationFailed = -7;
constexpr int kUploadFailed = -8;
constexpr int kDispatchFailed = -9;
constexpr int kReadbackRequestFailed = -10;
constexpr int kReadbackVerificationFailed = -11;
constexpr int kDeviceLost = -12;
constexpr int kInstanceCreationFailed = -13;
constexpr int kAdapterRequestCancelled = -14;
constexpr int kAdapterRequestFailed = -15;
constexpr int kNullAdapter = -16;
constexpr int kDeviceRequestCancelled = -17;
constexpr int kDeviceRequestFailed = -18;
constexpr int kNullDevice = -19;
constexpr int kDeviceError = -20;

std::atomic<int> g_status{kPending};
WGPUInstance g_instance = nullptr;
std::unique_ptr<tess::gpu::WebGpuBackend> g_backend;
// Device callbacks may arrive while run_compute is constructing the owner.
// Publish a separate atomic observer pointer only after construction; an
// earlier callback leaves a terminal status that run_compute forwards once
// the backend becomes visible.
std::atomic<tess::gpu::WebGpuBackend*> g_backend_visible{nullptr};

bool transition_status(int from, int to) noexcept {
  return g_status.compare_exchange_strong(from, to, std::memory_order_acq_rel);
}

[[nodiscard]] WGPUStringView string_view(const char* text) noexcept {
  return WGPUStringView{text, WGPU_STRLEN};
}

void release_instance() noexcept {
  if (g_instance != nullptr) {
    wgpuInstanceRelease(g_instance);
    g_instance = nullptr;
  }
}

void finish_readback(tess::gpu::GpuProductHandle,
                     tess::gpu::WebGpuReadbackStatus status, const void* data,
                     std::size_t size, void*) noexcept {
  // Device loss can race ahead of the map callback. Preserve that earlier
  // terminal failure instead of relabeling it as a readback result.
  if (g_status.load(std::memory_order_acquire) != kAwaitingReadback) {
    return;
  }
  constexpr std::array<std::uint32_t, 4> expected{2, 4, 6, 8};
  if (status != tess::gpu::WebGpuReadbackStatus::Complete ||
      size != sizeof(expected) || data == nullptr) {
    auto expected_status = kAwaitingReadback;
    g_status.compare_exchange_strong(expected_status,
                                     kReadbackVerificationFailed,
                                     std::memory_order_acq_rel);
    return;
  }
  const auto* values = static_cast<const std::uint32_t*>(data);
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (values[index] != expected[index]) {
      auto expected_status = kAwaitingReadback;
      g_status.compare_exchange_strong(expected_status,
                                       kReadbackVerificationFailed,
                                       std::memory_order_acq_rel);
      return;
    }
  }
  auto expected_status = kAwaitingReadback;
  g_status.compare_exchange_strong(expected_status, kReady,
                                   std::memory_order_acq_rel);
}

void device_lost(const WGPUDevice*, WGPUDeviceLostReason, WGPUStringView, void*,
                 void*) {
  if (auto* backend = g_backend_visible.load(std::memory_order_acquire);
      backend != nullptr) {
    backend->notify_device_lost();
  }
  auto status = g_status.load(std::memory_order_acquire);
  while ((status == kPending || status >= kRequestingDevice) &&
         !g_status.compare_exchange_weak(status, kDeviceLost,
                                         std::memory_order_acq_rel)) {
  }
}

void device_error(const WGPUDevice*, WGPUErrorType type, WGPUStringView, void*,
                  void*) {
  // Submission methods cannot synchronously observe validation, OOM, or
  // internal device errors. Forward the application-owned callback into the
  // backend's fail-closed state before publishing the browser-smoke failure.
  if (type == WGPUErrorType_NoError) {
    return;
  }
  if (auto* backend = g_backend_visible.load(std::memory_order_acquire);
      backend != nullptr) {
    backend->notify_device_error();
  }
  auto status = g_status.load(std::memory_order_acquire);
  while ((status == kPending || status >= kRequestingDevice) &&
         !g_status.compare_exchange_weak(status, kDeviceError,
                                         std::memory_order_acq_rel)) {
  }
}

[[nodiscard]] bool run_compute(WGPUDevice device) {
  g_backend = std::make_unique<tess::gpu::WebGpuBackend>(
      device, tess::gpu::WebGpuBackendConfig{
                  .max_buffer_bytes = 4096,
                  .max_dispatch_chunks = 1,
                  .max_inflight_readback_bytes = 64,
                  .field_capacity = 1,
                  .product_capacity = 1,
              });
  g_backend_visible.store(g_backend.get(), std::memory_order_release);
  // The uncaptured-error callback belongs to the device descriptor and may
  // run while this constructor is acquiring the borrowed device/queue. If it
  // arrived before g_backend became visible, apply the deferred fail-closed
  // notification now and preserve its terminal browser status.
  const auto initial_status = g_status.load(std::memory_order_acquire);
  if (initial_status == kDeviceLost || initial_status == kDeviceError) {
    if (initial_status == kDeviceLost) {
      g_backend->notify_device_lost();
    } else {
      g_backend->notify_device_error();
    }
    return false;
  }
  const auto field = tess::gpu::FieldMirrorDesc{
      .field_index = 0,
      .format = tess::gpu::GpuFieldFormat::U32,
      .value_bytes = 4,
      .tiles_per_chunk = 4,
      .bytes_per_chunk = 16,
      .chunk_count = 1,
  };
  if (!g_backend->register_field(field)) {
    transition_status(kRunningCompute, kFieldRegistrationFailed);
    return false;
  }

  auto shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
  shader_source.code = string_view(kShader);
  auto shader_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  shader_desc.nextInChain = &shader_source.chain;
  auto shader = wgpuDeviceCreateShaderModule(device, &shader_desc);
  if (shader == nullptr) {
    transition_status(kRunningCompute, kShaderCreationFailed);
    return false;
  }
  auto pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
  pipeline_desc.compute.module = shader;
  pipeline_desc.compute.entryPoint = string_view("main");
  auto pipeline = wgpuDeviceCreateComputePipeline(device, &pipeline_desc);
  wgpuShaderModuleRelease(shader);
  if (pipeline == nullptr) {
    transition_status(kRunningCompute, kPipelineCreationFailed);
    return false;
  }

  auto output_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  output_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
  output_desc.size = 16;
  auto output = wgpuDeviceCreateBuffer(device, &output_desc);
  auto layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
  if (output == nullptr || layout == nullptr) {
    transition_status(kRunningCompute, kOutputSetupFailed);
    if (output != nullptr) {
      wgpuBufferRelease(output);
    }
    if (layout != nullptr) {
      wgpuBindGroupLayoutRelease(layout);
    }
    wgpuComputePipelineRelease(pipeline);
    return false;
  }

  std::array<WGPUBindGroupEntry, 2> entries{
      WGPU_BIND_GROUP_ENTRY_INIT,
      WGPU_BIND_GROUP_ENTRY_INIT,
  };
  entries[0].binding = 0;
  entries[0].buffer = g_backend->field_buffer(0);
  entries[0].size = 16;
  entries[1].binding = 1;
  entries[1].buffer = output;
  entries[1].size = 16;
  auto bind_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  bind_desc.layout = layout;
  bind_desc.entryCount = entries.size();
  bind_desc.entries = entries.data();
  auto bind_group = wgpuDeviceCreateBindGroup(device, &bind_desc);
  wgpuBindGroupLayoutRelease(layout);
  if (bind_group == nullptr) {
    transition_status(kRunningCompute, kBindGroupCreationFailed);
    wgpuBufferRelease(output);
    wgpuComputePipelineRelease(pipeline);
    return false;
  }

  const auto product = g_backend->register_product(tess::gpu::WebGpuProductDesc{
      .product_key = 1,
      .input_field_index = 0,
      .pipeline = pipeline,
      .bind_group = bind_group,
      .readback_source = output,
      .readback_byte_size = 16,
      .readback_callback = finish_readback,
  });
  wgpuBindGroupRelease(bind_group);
  wgpuBufferRelease(output);
  wgpuComputePipelineRelease(pipeline);
  if (!product.has_value()) {
    transition_status(kRunningCompute, kProductRegistrationFailed);
    return false;
  }

  constexpr std::array<std::uint32_t, 4> input{1, 2, 3, 4};
  if (!g_backend->upload(tess::gpu::UploadDesc{
          .field_index = 0,
          .byte_size = sizeof(input),
          .data = input.data(),
      })) {
    transition_status(kRunningCompute, kUploadFailed);
    return false;
  }
  if (!g_backend->dispatch(tess::gpu::DispatchDesc{
          .product_key = product->key,
          .product_generation = product->generation,
          .input_field_index = 0,
          .chunk_count = 1,
      })) {
    transition_status(kRunningCompute, kDispatchFailed);
    return false;
  }
  // Device callbacks may run on another thread. Transition only from the
  // phase we own, so a terminal device status that wins this race is never
  // overwritten by the later readback label.
  auto running = kRunningCompute;
  if (!g_status.compare_exchange_strong(running, kAwaitingReadback,
                                        std::memory_order_acq_rel)) {
    return false;
  }
  if (!g_backend->readback(tess::gpu::ReadbackDesc{
          .product_key = product->key,
          .product_generation = product->generation,
          .policy = tess::gpu::ReadbackPolicy::Summary,
          .byte_size = 16,
      })) {
    auto awaiting = kAwaitingReadback;
    g_status.compare_exchange_strong(awaiting, kReadbackRequestFailed,
                                     std::memory_order_acq_rel);
    return false;
  }
  return true;
}

void device_ready(WGPURequestDeviceStatus status, WGPUDevice device,
                  WGPUStringView, void*, void*) {
  release_instance();
  const auto prior_status = g_status.load(std::memory_order_acquire);
  if (prior_status == kDeviceLost || prior_status == kDeviceError) {
    if (device != nullptr) {
      wgpuDeviceRelease(device);
    }
    return;
  }
  if (status != WGPURequestDeviceStatus_Success) {
    if (status == WGPURequestDeviceStatus_CallbackCancelled) {
      transition_status(kRequestingDevice, kDeviceRequestCancelled);
    } else {
      transition_status(kRequestingDevice, kDeviceRequestFailed);
    }
    if (device != nullptr) {
      wgpuDeviceRelease(device);
    }
    return;
  }
  if (device == nullptr) {
    transition_status(kRequestingDevice, kNullDevice);
    return;
  }
  auto requesting = kRequestingDevice;
  if (!g_status.compare_exchange_strong(requesting, kRunningCompute,
                                        std::memory_order_acq_rel)) {
    wgpuDeviceRelease(device);
    return;
  }
  if (!run_compute(device)) {
    auto running_status = kRunningCompute;
    g_status.compare_exchange_strong(running_status, kFieldRegistrationFailed,
                                     std::memory_order_acq_rel);
  }
  wgpuDeviceRelease(device);
}

void adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                   WGPUStringView, void*, void*) {
  if (status != WGPURequestAdapterStatus_Success) {
    // Only the API's explicit "no adapter exists" result is unsupported.
    // Cancellation and errors indicate a broken smoke run, not capability.
    if (status == WGPURequestAdapterStatus_Unavailable) {
      g_status = kAdapterUnavailable;
    } else if (status == WGPURequestAdapterStatus_CallbackCancelled) {
      g_status = kAdapterRequestCancelled;
    } else {
      g_status = kAdapterRequestFailed;
    }
    if (adapter != nullptr) {
      wgpuAdapterRelease(adapter);
    }
    release_instance();
    return;
  }
  if (adapter == nullptr) {
    g_status = kNullAdapter;
    release_instance();
    return;
  }
  g_status = kRequestingDevice;
  auto device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
  // Stable C mode zero is valid only when the callback is null. Without this
  // explicit mode Emdawn rejects the descriptor and returns a null future.
  device_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
  device_desc.deviceLostCallbackInfo.callback = device_lost;
  // WebGpuBackend deliberately does not replace application callbacks on a
  // borrowed device. This example therefore owns the callback and forwards
  // every uncaptured device error while the process-lifetime backend is live.
  device_desc.uncapturedErrorCallbackInfo.callback = device_error;
  auto callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  callback.mode = WGPUCallbackMode_AllowSpontaneous;
  callback.callback = device_ready;
  // Backends may keep asynchronous request machinery on the instance even
  // with spontaneous callbacks. Keep our last reference until device_ready so
  // native and browser implementations see an unambiguous lifetime.
  const auto device_future =
      wgpuAdapterRequestDevice(adapter, &device_desc, callback);
  if (device_future.id == 0 &&
      transition_status(kRequestingDevice, kDeviceRequestFailed)) {
    release_instance();
  }
  wgpuAdapterRelease(adapter);
}

}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int tess_webgpu_status() {
  return g_status.load(std::memory_order_acquire);
}

int main() {
  g_instance = wgpuCreateInstance(nullptr);
  if (g_instance == nullptr) {
    transition_status(kPending, kInstanceCreationFailed);
    return 0;
  }
  auto callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  callback.mode = WGPUCallbackMode_AllowSpontaneous;
  callback.callback = adapter_ready;
  const auto adapter_future =
      wgpuInstanceRequestAdapter(g_instance, nullptr, callback);
  if (adapter_future.id == 0 &&
      transition_status(kPending, kAdapterRequestFailed)) {
    release_instance();
  }
  return 0;
}

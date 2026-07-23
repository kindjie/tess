#include <webgpu/webgpu.h>

#define TESS_ENABLE_WEBGPU
#include <emscripten/emscripten.h>
#include <tess/gpu/webgpu_backend.h>

#include <array>
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

int g_status = 0;
WGPUInstance g_instance = nullptr;
std::unique_ptr<tess::gpu::WebGpuBackend> g_backend;

[[nodiscard]] WGPUStringView string_view(const char* text) noexcept {
  return WGPUStringView{text, WGPU_STRLEN};
}

void finish_readback(tess::gpu::GpuProductHandle,
                     tess::gpu::WebGpuReadbackStatus status, const void* data,
                     std::size_t size, void*) noexcept {
  constexpr std::array<std::uint32_t, 4> expected{2, 4, 6, 8};
  if (status != tess::gpu::WebGpuReadbackStatus::Complete ||
      size != sizeof(expected) || data == nullptr) {
    g_status = -11;
    return;
  }
  const auto* values = static_cast<const std::uint32_t*>(data);
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (values[index] != expected[index]) {
      g_status = -11;
      return;
    }
  }
  g_status = 1;
}

void device_lost(const WGPUDevice*, WGPUDeviceLostReason, WGPUStringView, void*,
                 void*) {
  if (g_backend != nullptr) {
    g_backend->notify_device_lost();
  }
  if (g_status == 0) {
    g_status = -12;
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
  const auto field = tess::gpu::FieldMirrorDesc{
      .field_index = 0,
      .format = tess::gpu::GpuFieldFormat::U32,
      .value_bytes = 4,
      .tiles_per_chunk = 4,
      .bytes_per_chunk = 16,
      .chunk_count = 1,
  };
  if (!g_backend->register_field(field)) {
    g_status = -2;
    return false;
  }

  auto shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
  shader_source.code = string_view(kShader);
  auto shader_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  shader_desc.nextInChain = &shader_source.chain;
  auto shader = wgpuDeviceCreateShaderModule(device, &shader_desc);
  if (shader == nullptr) {
    g_status = -3;
    return false;
  }
  auto pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
  pipeline_desc.compute.module = shader;
  pipeline_desc.compute.entryPoint = string_view("main");
  auto pipeline = wgpuDeviceCreateComputePipeline(device, &pipeline_desc);
  wgpuShaderModuleRelease(shader);
  if (pipeline == nullptr) {
    g_status = -4;
    return false;
  }

  auto output_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
  output_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
  output_desc.size = 16;
  auto output = wgpuDeviceCreateBuffer(device, &output_desc);
  auto layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
  if (output == nullptr || layout == nullptr) {
    g_status = -5;
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
    g_status = -6;
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
    g_status = -7;
    return false;
  }

  constexpr std::array<std::uint32_t, 4> input{1, 2, 3, 4};
  if (!g_backend->upload(tess::gpu::UploadDesc{
          .field_index = 0,
          .byte_size = sizeof(input),
          .data = input.data(),
      })) {
    g_status = -8;
    return false;
  }
  if (!g_backend->dispatch(tess::gpu::DispatchDesc{
          .product_key = product->key,
          .product_generation = product->generation,
          .input_field_index = 0,
          .chunk_count = 1,
      })) {
    g_status = -9;
    return false;
  }
  if (!g_backend->readback(tess::gpu::ReadbackDesc{
          .product_key = product->key,
          .product_generation = product->generation,
          .policy = tess::gpu::ReadbackPolicy::Summary,
          .byte_size = 16,
      })) {
    g_status = -10;
    return false;
  }
  return true;
}

void device_ready(WGPURequestDeviceStatus status, WGPUDevice device,
                  WGPUStringView, void*, void*) {
  if (status != WGPURequestDeviceStatus_Success || device == nullptr) {
    g_status = -1;
    return;
  }
  if (!run_compute(device)) {
    if (g_status == 0) {
      g_status = -2;
    }
  }
  wgpuDeviceRelease(device);
}

void adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                   WGPUStringView, void*, void*) {
  if (status != WGPURequestAdapterStatus_Success || adapter == nullptr) {
    g_status = -1;
    wgpuInstanceRelease(g_instance);
    g_instance = nullptr;
    return;
  }
  auto device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
  device_desc.deviceLostCallbackInfo.callback = device_lost;
  auto callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  callback.mode = WGPUCallbackMode_AllowSpontaneous;
  callback.callback = device_ready;
  static_cast<void>(wgpuAdapterRequestDevice(adapter, &device_desc, callback));
  wgpuAdapterRelease(adapter);
  wgpuInstanceRelease(g_instance);
  g_instance = nullptr;
}

}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int tess_webgpu_status() { return g_status; }

int main() {
  g_instance = wgpuCreateInstance(nullptr);
  if (g_instance == nullptr) {
    g_status = -1;
    return 0;
  }
  auto callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  callback.mode = WGPUCallbackMode_AllowSpontaneous;
  callback.callback = adapter_ready;
  static_cast<void>(wgpuInstanceRequestAdapter(g_instance, nullptr, callback));
  return 0;
}

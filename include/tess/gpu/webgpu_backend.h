#pragma once

// Optional WebGPU C backend. The consumer supplies and includes the stable
// <webgpu/webgpu.h> API before this header and links Dawn/Emdawnwebgpu (or a
// compatible implementation). CPU-only builds never parse or link this code.

#if defined(TESS_ENABLE_WEBGPU)

#ifndef WEBGPU_H_
#error "tess/gpu/webgpu_backend.h requires <webgpu/webgpu.h> first"
#endif

#include <tess/gpu/backend.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <vector>

namespace tess::gpu {

/** Fixed budgets and registry capacities for one WebGPU backend instance. */
struct WebGpuBackendConfig {
  std::uint64_t max_buffer_bytes = std::uint64_t{256} * 1024u * 1024u;
  std::uint64_t max_dispatch_chunks = 65535;
  std::uint64_t max_inflight_readback_bytes = std::uint64_t{4} * 1024u * 1024u;
  std::size_t field_capacity = 16;
  std::size_t product_capacity = 64;
  bool allow_full_field_readback = false;
};

/** Terminal state delivered to a WebGPU readback callback. */
enum class WebGpuReadbackStatus : std::uint8_t {
  Complete,
  Failed,
};

/** Non-throwing application callback for one explicit readback request. */
using WebGpuReadbackCallback = void (*)(GpuProductHandle, WebGpuReadbackStatus,
                                        const void*, std::size_t,
                                        void*) noexcept;

/**
 * Application-created algorithm resources registered under one product key.
 *
 * Registration retains the pipeline, bind group, and optional readback source.
 * The application may release its references immediately afterward. Shader,
 * layout, and binding construction stay with the algorithm provider. Readback
 * callback userdata must remain valid until that callback runs.
 */
struct WebGpuProductDesc {
  std::uint64_t product_key = 0;
  std::uint32_t input_field_index = 0;
  WGPUComputePipeline pipeline = nullptr;
  WGPUBindGroup bind_group = nullptr;
  WGPUBuffer readback_source = nullptr;
  std::uint64_t readback_source_offset = 0;
  std::uint64_t readback_byte_size = 0;
  WebGpuReadbackCallback readback_callback = nullptr;
  void* readback_userdata = nullptr;
};

namespace detail {

struct WebGpuSharedState {
  std::atomic<bool> available{true};
  std::atomic<std::uint64_t> inflight_readback_bytes{0};
  std::uint64_t max_inflight_readback_bytes = 0;
};

struct WebGpuReadbackOperation {
  WGPUBuffer staging = nullptr;
  GpuProductHandle handle{};
  std::size_t byte_size = 0;
  WebGpuReadbackCallback callback = nullptr;
  void* userdata = nullptr;
  std::shared_ptr<WebGpuSharedState> shared;
};

inline void webgpu_readback_complete(WGPUMapAsyncStatus status, WGPUStringView,
                                     void* userdata1, void*) noexcept {
  auto* operation = static_cast<WebGpuReadbackOperation*>(userdata1);
  const void* data = nullptr;
  auto result = WebGpuReadbackStatus::Failed;
  if (status == WGPUMapAsyncStatus_Success) {
    data = wgpuBufferGetConstMappedRange(operation->staging, 0,
                                         operation->byte_size);
    if (data != nullptr) {
      result = WebGpuReadbackStatus::Complete;
    }
  }
  operation->callback(operation->handle, result, data,
                      data == nullptr ? 0 : operation->byte_size,
                      operation->userdata);
  if (status == WGPUMapAsyncStatus_Success) {
    wgpuBufferUnmap(operation->staging);
  }
  wgpuBufferRelease(operation->staging);
  operation->shared->inflight_readback_bytes.fetch_sub(
      operation->byte_size, std::memory_order_relaxed);
  delete operation;
}

[[nodiscard]] inline bool reserve_readback_bytes(
    const std::shared_ptr<WebGpuSharedState>& shared,
    std::uint64_t bytes) noexcept {
  auto current =
      shared->inflight_readback_bytes.load(std::memory_order_relaxed);
  for (;;) {
    if (bytes > shared->max_inflight_readback_bytes - current) {
      return false;
    }
    if (shared->inflight_readback_bytes.compare_exchange_weak(
            current, current + bytes, std::memory_order_relaxed)) {
      return true;
    }
  }
}

}  // namespace detail

/**
 * Optional stable-C-API WebGPU transport backend.
 *
 * The backend owns field mirror buffers and retained algorithm handles. It
 * submits compute asynchronously; a successful boolean means submitted, not
 * completed. CPU state remains authoritative. Call `notify_device_lost()` from
 * the application's device-lost callback so all later work refuses cleanly.
 */
class WebGpuBackend {
 public:
  /** Retains `device`, obtains its queue, and reserves fixed registries. */
  explicit WebGpuBackend(WGPUDevice device, WebGpuBackendConfig config = {})
      : device_(device),
        config_(config),
        shared_(std::make_shared<detail::WebGpuSharedState>()) {
    shared_->max_inflight_readback_bytes = config_.max_inflight_readback_bytes;
    fields_.reserve(config_.field_capacity);
    products_.reserve(config_.product_capacity);
    if (device_ == nullptr || config_.max_buffer_bytes == 0 ||
        config_.max_dispatch_chunks == 0) {
      shared_->available.store(false, std::memory_order_relaxed);
      // No reference was retained on this disabled construction path.
      device_ = nullptr;
      return;
    }
    wgpuDeviceAddRef(device_);
    queue_ = wgpuDeviceGetQueue(device_);
    if (queue_ == nullptr) {
      shared_->available.store(false, std::memory_order_relaxed);
    }
  }

  WebGpuBackend(const WebGpuBackend&) = delete;
  auto operator=(const WebGpuBackend&) -> WebGpuBackend& = delete;
  WebGpuBackend(WebGpuBackend&&) = delete;
  auto operator=(WebGpuBackend&&) -> WebGpuBackend& = delete;

  /** Releases registries and retained device objects; readbacks remain safe. */
  ~WebGpuBackend() {
    for (auto& product : products_) {
      release_product(product);
    }
    for (auto& field : fields_) {
      if (field.buffer != nullptr) {
        wgpuBufferRelease(field.buffer);
      }
    }
    if (queue_ != nullptr) {
      wgpuQueueRelease(queue_);
    }
    if (device_ != nullptr) {
      wgpuDeviceRelease(device_);
    }
  }

  /** Reports configured limits, or no compute after invalidation/device loss.
   */
  [[nodiscard]] auto capabilities() const noexcept -> GpuCapabilities {
    if (!available()) {
      return {};
    }
    return GpuCapabilities{
        .compute = true,
        .async_dispatch = true,
        .async_readback = true,
        .max_buffer_bytes = config_.max_buffer_bytes,
        .max_dispatch_chunks = config_.max_dispatch_chunks,
        .buffer_alignment = 4,
    };
  }

  /** Creates a chunk-major storage/copy-destination buffer for one field. */
  [[nodiscard]] bool register_field(FieldMirrorDesc desc) {
    if (!available() || desc.total_bytes() == 0 ||
        desc.total_bytes() > config_.max_buffer_bytes ||
        fields_.size() >= config_.field_capacity ||
        find_field(desc.field_index) != nullptr) {
      return false;
    }
    const auto bytes = aligned_size(desc.total_bytes());
    if (!bytes.has_value() || *bytes > config_.max_buffer_bytes) {
      return false;
    }
    auto buffer_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    buffer_desc.size = *bytes;
    auto buffer = wgpuDeviceCreateBuffer(device_, &buffer_desc);
    if (buffer == nullptr) {
      return false;
    }
    fields_.push_back(FieldSlot{desc, buffer, *bytes});
    return true;
  }

  /** Returns a registered mirror buffer, or null when the field is absent. */
  [[nodiscard]] auto field_buffer(std::uint32_t field_index) const noexcept
      -> WGPUBuffer {
    const auto* field = find_field(field_index);
    return field == nullptr ? nullptr : field->buffer;
  }

  /** Retains one algorithm pipeline/bind group under a generation handle. */
  [[nodiscard]] auto register_product(WebGpuProductDesc desc)
      -> std::optional<GpuProductHandle> {
    if (!available() || desc.product_key == 0 || desc.pipeline == nullptr ||
        desc.bind_group == nullptr ||
        find_product(desc.product_key) != nullptr) {
      return std::nullopt;
    }
    if ((desc.readback_source == nullptr) != (desc.readback_byte_size == 0)) {
      return std::nullopt;
    }
    if (desc.readback_source != nullptr &&
        (desc.readback_callback == nullptr ||
         (desc.readback_source_offset & 3u) != 0 ||
         (desc.readback_byte_size & 3u) != 0 ||
         desc.readback_source_offset >
             wgpuBufferGetSize(desc.readback_source) ||
         desc.readback_byte_size > wgpuBufferGetSize(desc.readback_source) -
                                       desc.readback_source_offset)) {
      return std::nullopt;
    }
    ProductSlot* slot = nullptr;
    for (auto& candidate : products_) {
      if (!candidate.active) {
        slot = &candidate;
        break;
      }
    }
    if (slot == nullptr) {
      if (products_.size() >= config_.product_capacity) {
        return std::nullopt;
      }
      products_.emplace_back();
      slot = &products_.back();
    }
    wgpuComputePipelineAddRef(desc.pipeline);
    wgpuBindGroupAddRef(desc.bind_group);
    if (desc.readback_source != nullptr) {
      wgpuBufferAddRef(desc.readback_source);
    }
    slot->active = true;
    slot->generation = ++generation_clock_;
    if (slot->generation == 0) {
      slot->generation = ++generation_clock_;
    }
    slot->desc = desc;
    return GpuProductHandle{desc.product_key, slot->generation};
  }

  /** Releases a currently valid product and invalidates its generation. */
  bool unregister_product(GpuProductHandle handle) noexcept {
    auto* product = find_product(handle.key);
    if (product == nullptr || product->generation != handle.generation) {
      return false;
    }
    release_product(*product);
    return true;
  }

  /** Returns whether `handle` still names its registered product interval. */
  [[nodiscard]] bool valid(GpuProductHandle handle) const noexcept {
    const auto* product = find_product(handle.key);
    return product != nullptr && product->generation == handle.generation;
  }

  /** Writes one aligned chunk field slice into its registered mirror. */
  [[nodiscard]] bool upload(const UploadDesc& upload) {
    const auto* field = find_field(upload.field_index);
    if (!available() || field == nullptr || upload.data == nullptr ||
        upload.byte_size == 0 || (upload.buffer_offset & 3u) != 0 ||
        (upload.byte_size & 3u) != 0 ||
        upload.buffer_offset > field->allocated_bytes ||
        upload.byte_size > field->allocated_bytes - upload.buffer_offset) {
      return false;
    }
    wgpuQueueWriteBuffer(queue_, field->buffer, upload.buffer_offset,
                         upload.data,
                         static_cast<std::size_t>(upload.byte_size));
    return true;
  }

  /** Encodes and submits one registered compute product asynchronously. */
  [[nodiscard]] bool dispatch(const DispatchDesc& dispatch) {
    const auto* product = find_product(dispatch.product_key);
    if (!available() || product == nullptr ||
        product->generation != dispatch.product_generation ||
        product->desc.input_field_index != dispatch.input_field_index ||
        dispatch.chunk_count == 0 || dispatch.workgroups_per_chunk == 0 ||
        dispatch.chunk_count > config_.max_dispatch_chunks ||
        dispatch.chunk_count > std::numeric_limits<std::uint32_t>::max() /
                                   dispatch.workgroups_per_chunk) {
      return false;
    }
    auto encoder_desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    auto encoder = wgpuDeviceCreateCommandEncoder(device_, &encoder_desc);
    if (encoder == nullptr) {
      return false;
    }
    auto pass_desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    auto pass = wgpuCommandEncoderBeginComputePass(encoder, &pass_desc);
    if (pass == nullptr) {
      wgpuCommandEncoderRelease(encoder);
      return false;
    }
    wgpuComputePassEncoderSetPipeline(pass, product->desc.pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, product->desc.bind_group, 0,
                                       nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(
        pass,
        static_cast<std::uint32_t>(dispatch.chunk_count) *
            dispatch.workgroups_per_chunk,
        1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    const auto submitted = submit(encoder);
    wgpuCommandEncoderRelease(encoder);
    return submitted;
  }

  /** Queues an explicit bounded copy/map readback and returns immediately. */
  [[nodiscard]] bool readback(const ReadbackDesc& readback) {
    const auto* product = find_product(readback.product_key);
    if (!available() || product == nullptr ||
        product->generation != readback.product_generation ||
        readback.policy == ReadbackPolicy::None ||
        (readback.policy == ReadbackPolicy::FullField &&
         !config_.allow_full_field_readback) ||
        readback.byte_size == 0 || (readback.byte_size & 3u) != 0 ||
        product->desc.readback_source == nullptr ||
        product->desc.readback_callback == nullptr ||
        readback.byte_size > product->desc.readback_byte_size ||
        readback.byte_size > std::numeric_limits<std::size_t>::max() ||
        !detail::reserve_readback_bytes(shared_, readback.byte_size)) {
      return false;
    }
    const auto size = static_cast<std::size_t>(readback.byte_size);
    auto buffer_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    buffer_desc.size = readback.byte_size;
    auto staging = wgpuDeviceCreateBuffer(device_, &buffer_desc);
    if (staging == nullptr) {
      release_readback_bytes(size);
      return false;
    }
    auto* operation = new (std::nothrow) detail::WebGpuReadbackOperation{
        staging,
        GpuProductHandle{readback.product_key, readback.product_generation},
        size,
        product->desc.readback_callback,
        product->desc.readback_userdata,
        shared_,
    };
    if (operation == nullptr) {
      wgpuBufferRelease(staging);
      release_readback_bytes(size);
      return false;
    }

    auto encoder_desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    auto encoder = wgpuDeviceCreateCommandEncoder(device_, &encoder_desc);
    if (encoder == nullptr) {
      abandon_readback(operation);
      return false;
    }
    wgpuCommandEncoderCopyBufferToBuffer(encoder, product->desc.readback_source,
                                         product->desc.readback_source_offset,
                                         staging, 0, readback.byte_size);
    const auto submitted = submit(encoder);
    wgpuCommandEncoderRelease(encoder);
    if (!submitted) {
      abandon_readback(operation);
      return false;
    }
    auto callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = detail::webgpu_readback_complete;
    callback.userdata1 = operation;
    static_cast<void>(
        wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, size, callback));
    return true;
  }

  /** Permanently disables new work after the application reports device loss.
   */
  void notify_device_lost() noexcept {
    shared_->available.store(false, std::memory_order_relaxed);
  }

 private:
  struct FieldSlot {
    FieldMirrorDesc desc{};
    WGPUBuffer buffer = nullptr;
    std::uint64_t allocated_bytes = 0;
  };

  struct ProductSlot {
    bool active = false;
    std::uint64_t generation = 0;
    WebGpuProductDesc desc{};
  };

  [[nodiscard]] bool available() const noexcept {
    return device_ != nullptr && queue_ != nullptr &&
           shared_->available.load(std::memory_order_relaxed);
  }

  [[nodiscard]] static auto aligned_size(std::uint64_t size) noexcept
      -> std::optional<std::uint64_t> {
    if (size > std::numeric_limits<std::uint64_t>::max() - 3) {
      return std::nullopt;
    }
    return (size + 3) & ~std::uint64_t{3};
  }

  [[nodiscard]] auto find_field(std::uint32_t field_index) noexcept
      -> FieldSlot* {
    for (auto& field : fields_) {
      if (field.desc.field_index == field_index) {
        return &field;
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto find_field(std::uint32_t field_index) const noexcept
      -> const FieldSlot* {
    for (const auto& field : fields_) {
      if (field.desc.field_index == field_index) {
        return &field;
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto find_product(std::uint64_t key) noexcept -> ProductSlot* {
    for (auto& product : products_) {
      if (product.active && product.desc.product_key == key) {
        return &product;
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto find_product(std::uint64_t key) const noexcept
      -> const ProductSlot* {
    for (const auto& product : products_) {
      if (product.active && product.desc.product_key == key) {
        return &product;
      }
    }
    return nullptr;
  }

  static void release_product(ProductSlot& product) noexcept {
    if (!product.active) {
      return;
    }
    wgpuComputePipelineRelease(product.desc.pipeline);
    wgpuBindGroupRelease(product.desc.bind_group);
    if (product.desc.readback_source != nullptr) {
      wgpuBufferRelease(product.desc.readback_source);
    }
    product.active = false;
    product.desc = {};
  }

  [[nodiscard]] bool submit(WGPUCommandEncoder encoder) {
    auto command_desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    auto command = wgpuCommandEncoderFinish(encoder, &command_desc);
    if (command == nullptr) {
      return false;
    }
    wgpuQueueSubmit(queue_, 1, &command);
    wgpuCommandBufferRelease(command);
    return true;
  }

  void release_readback_bytes(std::size_t size) noexcept {
    shared_->inflight_readback_bytes.fetch_sub(size, std::memory_order_relaxed);
  }

  void abandon_readback(detail::WebGpuReadbackOperation* operation) noexcept {
    wgpuBufferRelease(operation->staging);
    release_readback_bytes(operation->byte_size);
    delete operation;
  }

  WGPUDevice device_ = nullptr;
  WGPUQueue queue_ = nullptr;
  WebGpuBackendConfig config_{};
  std::shared_ptr<detail::WebGpuSharedState> shared_;
  std::vector<FieldSlot> fields_;
  std::vector<ProductSlot> products_;
  std::uint64_t generation_clock_ = 0;
};

static_assert(GpuBackend<WebGpuBackend>);

}  // namespace tess::gpu

#endif  // defined(TESS_ENABLE_WEBGPU)

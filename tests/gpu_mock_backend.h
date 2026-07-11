#pragma once

#include <tess/gpu/backend.h>

#include <cstdint>
#include <vector>

// Test-only GPU backend: satisfies the GpuBackend concept, accepts
// everything its configured capabilities allow, and records the call
// sequence so tests can assert upload -> dispatch -> readback ordering
// and per-call payloads. May allocate freely.
namespace tess_test {

enum class GpuCallKind : std::uint8_t { Upload, Dispatch, Readback };

struct GpuCallRecord {
  GpuCallKind kind = GpuCallKind::Upload;
  // Upload: chunk key value / field index / byte size.
  // Dispatch: product key in `key`, chunk count in `bytes`.
  // Readback: product key in `key`, byte size in `bytes`,
  //           policy in `field_index`.
  std::uint64_t key = 0;
  std::uint32_t field_index = 0;
  std::uint64_t bytes = 0;
  const void* data = nullptr;
};

class MockGpuBackend {
 public:
  explicit MockGpuBackend(tess::gpu::GpuCapabilities capabilities =
                              tess::gpu::GpuCapabilities{
                                  .compute = true,
                                  .async_dispatch = true,
                                  .async_readback = true,
                                  .max_buffer_bytes = 1ULL << 30U,
                                  .max_dispatch_chunks = 1ULL << 20U,
                                  .buffer_alignment = 256,
                              })
      : capabilities_(capabilities) {}

  [[nodiscard]] auto capabilities() const noexcept
      -> tess::gpu::GpuCapabilities {
    return capabilities_;
  }

  auto upload(const tess::gpu::UploadDesc& upload) -> bool {
    if (!capabilities_.compute || upload.buffer_offset + upload.byte_size >
                                      capabilities_.max_buffer_bytes) {
      return false;
    }
    calls_.push_back(GpuCallRecord{GpuCallKind::Upload, upload.chunk_key.value,
                                   upload.field_index, upload.byte_size,
                                   upload.data});
    return true;
  }

  auto dispatch(const tess::gpu::DispatchDesc& dispatch) -> bool {
    if (!capabilities_.compute ||
        dispatch.chunk_count > capabilities_.max_dispatch_chunks) {
      return false;
    }
    calls_.push_back(GpuCallRecord{GpuCallKind::Dispatch, dispatch.product_key,
                                   dispatch.input_field_index,
                                   dispatch.chunk_count, nullptr});
    return true;
  }

  auto readback(const tess::gpu::ReadbackDesc& readback) -> bool {
    if (!capabilities_.compute ||
        readback.policy == tess::gpu::ReadbackPolicy::None) {
      return false;
    }
    calls_.push_back(GpuCallRecord{GpuCallKind::Readback, readback.product_key,
                                   static_cast<std::uint32_t>(readback.policy),
                                   readback.byte_size, nullptr});
    return true;
  }

  [[nodiscard]] auto calls() const noexcept
      -> const std::vector<GpuCallRecord>& {
    return calls_;
  }

  void clear() noexcept { calls_.clear(); }

 private:
  tess::gpu::GpuCapabilities capabilities_;
  std::vector<GpuCallRecord> calls_;
};

static_assert(tess::gpu::GpuBackend<MockGpuBackend>);

}  // namespace tess_test

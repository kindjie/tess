#pragma once

#include <tess/gpu/descriptors.h>

#include <concepts>
#include <cstdint>

// M13 GPU backend concept: compile-time polymorphic, supplied by the
// game or a plugin, never required. tess defines what a backend must
// answer (capabilities) and accept (uploads, dispatches, readbacks);
// NoGpuBackend is the default that compiles everywhere and refuses
// everything, so CPU-only builds carry zero GPU obligations.
namespace tess::gpu {

/**
 * Device limits and features consulted before selecting GPU execution.
 *
 * An all-zero value advertises no GPU execution path.
 */
struct GpuCapabilities {
  bool compute = false;
  bool async_dispatch = false;
  bool async_readback = false;
  std::uint64_t max_buffer_bytes = 0;
  std::uint64_t max_dispatch_chunks = 0;
  std::uint32_t buffer_alignment = 0;

  friend constexpr auto operator==(const GpuCapabilities&,
                                   const GpuCapabilities&) noexcept
      -> bool = default;
};

/**
 * Compile-time contract for an optional GPU execution backend.
 *
 * Operations return false when refused, allowing the authoritative CPU path
 * to take over without interpreting backend-specific errors.
 */
template <typename B>
concept GpuBackend =
    requires(B& backend, const B& const_backend, const UploadDesc& upload,
             const DispatchDesc& dispatch, const ReadbackDesc& readback) {
      {
        const_backend.capabilities()
      } noexcept -> std::same_as<GpuCapabilities>;
      { backend.upload(upload) } -> std::same_as<bool>;
      { backend.dispatch(dispatch) } -> std::same_as<bool>;
      { backend.readback(readback) } -> std::same_as<bool>;
    };

/** CPU-only backend that advertises no capabilities and refuses all work. */
struct NoGpuBackend {
  [[nodiscard]] constexpr auto capabilities() const noexcept
      -> GpuCapabilities {
    return GpuCapabilities{};
  }

  [[nodiscard]] constexpr auto upload(const UploadDesc&) noexcept -> bool {
    return false;
  }

  [[nodiscard]] constexpr auto dispatch(const DispatchDesc&) noexcept -> bool {
    return false;
  }

  [[nodiscard]] constexpr auto readback(const ReadbackDesc&) noexcept -> bool {
    return false;
  }
};

static_assert(GpuBackend<NoGpuBackend>);

}  // namespace tess::gpu

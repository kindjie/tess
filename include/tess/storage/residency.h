#pragma once

#include <tess/core/shape.h>

#include <cstddef>
#include <cstdint>

namespace tess {

/**
 * Selects the sparse `World` specialization.
 *
 * Only a byte-budgeted subset of a bounded shape is materialized, with
 * least-recently-used eviction. Contrast `AlwaysResident`, which eagerly
 * allocates every chunk.
 */
struct SparseResident {};

/** Construction parameters for a `SparseResidentWorld`. */
struct ResidencyConfig {
  /**
   * Maximum bytes of resident chunk-page storage.
   *
   * Capacity is `byte_budget / page_byte_size`, clamped to at least one page.
   * Bookkeeping storage is allocated separately and is not included.
   */
  std::size_t byte_budget = 0;
};

/**
 * Generation-stamped identity for one residency interval of a chunk.
 *
 * This is a value token, not an owning reference. Validate it against the
 * originating world with `World::valid()` before use. Eviction invalidates
 * the handle; reloading the same key receives a newer generation and does not
 * make an old handle valid again. Handles are not transferable between worlds.
 */
struct ResidencyHandle {
  ChunkKey key{};
  std::uint64_t generation = 0;

  friend constexpr bool operator==(ResidencyHandle lhs,
                                   ResidencyHandle rhs) noexcept = default;
};

}  // namespace tess

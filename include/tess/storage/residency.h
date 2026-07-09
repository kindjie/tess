#pragma once

#include <tess/core/shape.h>

#include <cstddef>
#include <cstdint>

namespace tess {

// Residency policy tag selecting the sparse World specialization: only a
// byte-budgeted subset of a (possibly enormous) bounded shape is ever
// materialized, with least-recently-used eviction. Contrast AlwaysResident,
// which eagerly allocates every chunk.
struct SparseResident {};

// Construction parameters for a SparseResident world. byte_budget caps the
// total bytes of resident chunk pages; the resident capacity is
// byte_budget / page_byte_size (at least one chunk).
struct ResidencyConfig {
  std::size_t byte_budget = 0;
};

// A generation-stamped reference to a resident chunk. The generation is a
// world-monotonic stamp assigned each time a chunk becomes resident, so a
// handle taken before an eviction never validates against the reloaded
// chunk (which reuses the key but receives a strictly greater generation).
struct ResidencyHandle {
  ChunkKey key{};
  std::uint64_t generation = 0;

  friend constexpr bool operator==(ResidencyHandle lhs,
                                   ResidencyHandle rhs) noexcept = default;
};

}  // namespace tess

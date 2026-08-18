#pragma once

#include <tess/core/shape.h>

#include <cstdint>

namespace tess {

/// Specifies inclusive start and goal coordinates for a path query.
struct PathRequest {
  Coord3 start;
  Coord3 goal;
};

/**
 * Selects a deterministic tertiary ordering among equal `(f, g)` A* nodes.
 *
 * Zero preserves the canonical tile-index ordering. A nonzero seed may choose
 * a different path only when candidates have identical search cost and
 * progress; it cannot relax passability or change the optimal result cost.
 */
struct PathTieBreak {
  std::uint64_t seed = 0;
};

}  // namespace tess

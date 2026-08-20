#pragma once

#include <tess/core/shape.h>

#include <cstdint>

namespace tess {

/// Specifies inclusive start and goal coordinates for a path query.
struct PathRequest {
  Coord3 start;
  Coord3 goal;
};

/// Selects how sparse searches report boundaries of the resident set.
enum class MissingChunkPolicy : std::uint8_t {
  // Treat a non-resident chunk as impassable. The search stays within the
  // resident set and may report NoPath even when a route exists through
  // chunks that are not currently materialized.
  AssumeImpassable,
  // Do not report NoPath across a non-resident boundary: if the search
  // exhausts the resident set having skipped at least one non-resident
  // neighbor, it returns Indeterminate instead of NoPath.
  ReportIndeterminate,
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

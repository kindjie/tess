#pragma once

#include <tess/core/shape.h>
#include <tess/path/path.h>

#include <ostream>

namespace tess {

/** Writes a `Coord2` in human-readable form. */
inline auto operator<<(std::ostream& output, Coord2 value) -> std::ostream& {
  return output << "Coord2{" << value.x << ", " << value.y << '}';
}

/** Writes a `Coord3` in human-readable form. */
inline auto operator<<(std::ostream& output, Coord3 value) -> std::ostream& {
  return output << "Coord3{" << value.x << ", " << value.y << ", " << value.z
                << '}';
}

/** Writes an `Extent3` in human-readable form. */
inline auto operator<<(std::ostream& output, Extent3 value) -> std::ostream& {
  return output << "Extent3{" << value.x << ", " << value.y << ", " << value.z
                << '}';
}

/** Writes a `PathStatus` name, or its numeric value when unrecognized. */
inline auto operator<<(std::ostream& output, PathStatus status)
    -> std::ostream& {
  switch (status) {
    case PathStatus::Found:
      return output << "Found";
    case PathStatus::InvalidStart:
      return output << "InvalidStart";
    case PathStatus::InvalidGoal:
      return output << "InvalidGoal";
    case PathStatus::NoPath:
      return output << "NoPath";
    case PathStatus::Indeterminate:
      return output << "Indeterminate";
    case PathStatus::CostOverflow:
      return output << "CostOverflow";
  }
  return output << "PathStatus(" << static_cast<unsigned>(status) << ')';
}

/**
 * Writes every coordinate in a borrowed path in human-readable form.
 *
 * The operation traverses the view and does not extend its backing storage's
 * lifetime. tess performs no allocation; the destination stream retains
 * control of its own buffering, error state, and exception policy.
 */
inline auto operator<<(std::ostream& output, PathView path) -> std::ostream& {
  output << '[';
  auto first = true;
  for (const auto coordinate : path) {
    if (!first) {
      output << ", ";
    }
    output << coordinate;
    first = false;
  }
  return output << ']';
}

}  // namespace tess

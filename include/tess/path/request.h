#pragma once

#include <tess/core/shape.h>

namespace tess {

/// Specifies inclusive start and goal coordinates for a path query.
struct PathRequest {
  Coord3 start;
  Coord3 goal;
};

}  // namespace tess

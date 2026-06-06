#pragma once

#include <tess/block/block.h>
#include <tess/core/shape.h>
#include <tess/diagnostics/diagnostics.h>
#include <tess/ops/queued.h>
#include <tess/path/distance_field_box.h>
#include <tess/path/path.h>
#include <tess/path/path_runtime.h>
#include <tess/path/portal_route.h>
#include <tess/path/portal_segment_cache.h>
#include <tess/storage/chunk_page.h>
#include <tess/storage/world.h>
#include <tess/topology/topology.h>

#define TESS_VERSION_MAJOR 0
#define TESS_VERSION_MINOR 2
#define TESS_VERSION_PATCH 0

namespace tess {

struct version {
  int major;
  int minor;
  int patch;
};

inline constexpr version library_version{
    TESS_VERSION_MAJOR,
    TESS_VERSION_MINOR,
    TESS_VERSION_PATCH,
};

}  // namespace tess

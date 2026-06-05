#pragma once

#include <tess/block/block.h>
#include <tess/core/shape.h>
#include <tess/ops/queued.h>
#include <tess/path/path.h>
#include <tess/storage/chunk_page.h>
#include <tess/storage/world.h>

#define TESS_VERSION_MAJOR 0
#define TESS_VERSION_MINOR 1
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

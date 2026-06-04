#pragma once

#include <tess/core/shape.h>
#include <tess/storage/chunk_page.h>

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

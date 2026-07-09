#pragma once

#include <tess/block/block.h>
#include <tess/core/shape.h>
#include <tess/diagnostics/diagnostics.h>
#include <tess/diagnostics/trace.h>
#include <tess/diagnostics/warning_sink.h>
#include <tess/ops/phase_executor.h>
#include <tess/ops/queued.h>
#include <tess/path/distance_field_box.h>
#include <tess/path/field_product_cache.h>
#include <tess/path/node_index_space.h>
#include <tess/path/path.h>
#include <tess/path/path_runtime.h>
#include <tess/path/path_view.h>
#include <tess/path/portal_route.h>
#include <tess/path/portal_segment_cache.h>
#include <tess/path/precheck.h>
#include <tess/path/route_cache.h>
#include <tess/sim/movement.h>
#include <tess/sim/path_agent.h>
#include <tess/sim/path_agent_tick.h>
#include <tess/sim/render_delta.h>
#include <tess/sim/scheduler.h>
#include <tess/sim/time.h>
#include <tess/storage/chunk_meta.h>
#include <tess/storage/chunk_page.h>
#include <tess/storage/residency.h>
#include <tess/storage/sparse_world.h>
#include <tess/storage/world.h>
#include <tess/topology/topology.h>

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

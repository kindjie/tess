// One macro-configuration cell. The build defines exactly one
// TESS_CONTRACT_CELL_* macro, and the corresponding preamble supplies
// whatever consumer-owned header the enabled integration documents as
// its prerequisite before the tess header that needs it.
//
// This is the coverage CI otherwise lacks: every dev preset pins EnTT
// and Flecs both on, and every consumer preset pins both off, so no
// build anywhere exercises one adapter without the other.

#if defined(TESS_CONTRACT_CELL_ENTT_ONLY)
#include <tess/ecs/entt/entt_adapter.h>

#include <entt/entt.hpp>
#endif

#if defined(TESS_CONTRACT_CELL_FLECS_ONLY)
#include <flecs.h>
#include <tess/ecs/flecs/flecs_adapter.h>
#endif

#if defined(TESS_CONTRACT_CELL_IMGUI)
#include <imgui.h>
#include <tess/debug/imgui/panels.h>
#include <tess/debug/imgui/tools.h>
#endif

#if defined(TESS_CONTRACT_CELL_WEBGPU)
#include <tess/gpu/webgpu_backend.h>
#include <webgpu/webgpu.h>
#endif

// Every cell also sweeps the unconditional public surface, so a cell's
// macro cannot break a header that is supposed to be independent of it.
#include <cstdint>

#include "tess_all_headers_forward.h"

namespace tess_test::contract {

// Linked so the cell is not merely compiled: an accidental non-inline
// definition reachable only under this configuration still surfaces.
auto macro_cell_probe() -> std::uintptr_t {
  struct CellTag {};
  return tess::detail::tag_identity<CellTag>();
}

}  // namespace tess_test::contract

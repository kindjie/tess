// One macro-configuration cell. The build defines exactly one
// TESS_CONTRACT_CELL_* macro, and the matching preamble supplies the
// consumer-owned header that the enabled integration documents as its
// prerequisite, before the tess header that needs it.
//
// This is the coverage CI otherwise lacks: every dev preset pins EnTT
// and Flecs both on, and every consumer preset pins both off, so no
// build anywhere exercises one adapter without the other.
//
// Include order here is a contract, not a style choice — each tess
// integration header checks for its prerequisite and #errors without
// it — so the block is fenced off from the formatter, which would
// otherwise sort the tess header ahead of what it depends on.

// clang-format off
#if defined(TESS_CONTRACT_CELL_ENTT_ONLY)
#include <entt/entt.hpp>
#include <tess/ecs/entt/entt_adapter.h>
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
#include <webgpu/webgpu.h>
#include <tess/gpu/webgpu_backend.h>
#endif
// clang-format on

// Every cell also sweeps the unconditional public surface, so a cell's
// macro cannot break a header that is meant to be independent of it.
#include <cstdint>

#include "tess_all_headers_forward.h"

#if defined(TESS_EXPECT_NO_EXCEPTIONS)
static_assert(TESS_HAS_EXCEPTIONS == 0);
static_assert(!tess::has_exceptions);
#endif

namespace tess_test::contract {

// Shared between the cell's two units so both name the same
// specialization.
struct CellProbeTag {};

// Each cell links TWO units built with the identical configuration,
// so a macro-gated definition that lost its inline becomes a
// duplicate symbol at link time. One unit alone cannot show that.
auto macro_cell_probe_first() -> std::uintptr_t {
  return tess::detail::tag_identity<CellProbeTag>();
}

}  // namespace tess_test::contract

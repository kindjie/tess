// Every public header, in reverse declaration order: a header that
// silently depends on a sibling appearing first fails here, and the
// canonical order every other consumer uses would hide it.
//
// As in the forward unit, the sweep must be the first tess include or
// the ordering it exists to test is already decided.

// clang-format off
#include "tess_all_headers_reverse.h"
// clang-format on

#include <cstdint>

#include "contract.h"
#include "probe_types.h"

namespace tess_test::contract {

auto tag_identity_reverse() -> std::uintptr_t {
  return tess::detail::tag_identity<ProbeTag>();
}

auto world_stamp_reverse() -> const void* {
  return tess::detail::planned_world_stamp<ProbeShape, kProbeChunkLimit>();
}

}  // namespace tess_test::contract

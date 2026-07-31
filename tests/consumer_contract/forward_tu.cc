// Every public header, in declaration order.
//
// The sweep must be the FIRST tess include in the unit, or the headers
// it is meant to parse in a specific order arrive already included and
// their guards make the sweep a no-op. That is why the block is fenced
// from the formatter, which sorts includes alphabetically and would
// put the sweep last.

// clang-format off
#include "tess_all_headers_forward.h"
// clang-format on

#include <cstdint>

#include "contract.h"
#include "probe_types.h"

namespace tess_test::contract {

auto tag_identity_forward() -> std::uintptr_t {
  return tess::detail::tag_identity<ProbeTag>();
}

auto world_stamp_forward() -> const void* {
  return tess::detail::planned_world_stamp<ProbeShape, kProbeChunkLimit>();
}

}  // namespace tess_test::contract

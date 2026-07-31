// Every public header, in declaration order.
#include "contract.h"
#include "probe_types.h"
#include "tess_all_headers_forward.h"

namespace tess_test::contract {

auto tag_identity_forward() -> std::uintptr_t {
  return tess::detail::tag_identity<ProbeTag>();
}

auto world_stamp_forward() -> const void* {
  return tess::detail::planned_world_stamp<ProbeShape, kProbeChunkLimit>();
}

}  // namespace tess_test::contract

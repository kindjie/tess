// Every public header, in reverse declaration order: a header that
// silently depends on a sibling appearing first fails here, and the
// canonical order every other consumer uses would hide it.
#include "contract.h"
#include "probe_types.h"
#include "tess_all_headers_reverse.h"

namespace tess_test::contract {

auto tag_identity_reverse() -> std::uintptr_t {
  return tess::detail::tag_identity<ProbeTag>();
}

auto world_stamp_reverse() -> const void* {
  return tess::detail::planned_world_stamp<ProbeShape, kProbeChunkLimit>();
}

}  // namespace tess_test::contract

// The types both probe translation units instantiate. Defined once so
// the two units genuinely name the SAME specialization: comparing
// identities of different types would prove nothing.
//
// This header deliberately includes only the narrow headers it needs,
// never an umbrella: pulling in tess.h here would pre-include most of
// the public surface and make the ordered sweeps in the probe units
// no-ops behind their include guards.
#pragma once

#include <tess/core/shape.h>

#include <cstdint>

namespace tess_test::contract {

struct ProbeTag {};

using ProbeShape =
    tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;

inline constexpr std::uint64_t kProbeChunkLimit = 16;

}  // namespace tess_test::contract

// Shared declarations for the multi-translation-unit consumer
// contract probes. Each translation unit includes the whole public
// header set in a different order and reports tess's own identity
// facilities; the test links them together and compares.
#pragma once

#include <cstdint>

namespace tess_test::contract {

// tag_identity() and planned_world_stamp() return the address of a
// static local inside an inline function, so every translation unit
// in a program must observe the SAME address. A header that lost its
// inline, or a facility that acquired per-translation-unit state,
// shows up here as a mismatch — and nowhere in a single-TU test.
auto tag_identity_forward() -> std::uintptr_t;
auto tag_identity_reverse() -> std::uintptr_t;
auto world_stamp_forward() -> const void*;
auto world_stamp_reverse() -> const void*;

// A consumer that includes only the narrowest owning headers, never an
// umbrella.
auto leaf_first_world_is_usable() -> bool;

}  // namespace tess_test::contract

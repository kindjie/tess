#pragma once

#include <cstdint>

namespace tess::detail {

// Runtime identity token for a compile-time tag or movement-class TYPE. Each
// instantiation owns one static byte, so the address is unique per type
// within a binary and stable for its lifetime — usable as a cache-key field
// or a graph/runtime stamp where the type itself cannot be stored. Zero is
// never returned, so 0 can mean "unbound".
template <typename Tag>
[[nodiscard]] inline auto tag_identity() noexcept -> std::uintptr_t {
  static const auto token = std::uint8_t{0};
  return reinterpret_cast<std::uintptr_t>(&token);
}

}  // namespace tess::detail

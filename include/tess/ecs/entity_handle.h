#pragma once

#include <cstdint>

// EntityHandle lives in its own header so non-ECS layers (the render
// delta bridge in sim/delta_frame.h) can name entity identity without
// pulling in the agent pipeline; <tess/ecs/adapter.h> re-exports it.
namespace tess {

// Opaque, ECS-agnostic entity identity: stable while tess holds it,
// comparable, with a null representation. Adapters pack their native id
// (including any generation/version bits) into the 64-bit value however
// they like; tess never interprets it.
struct EntityHandle {
  std::uint64_t value = 0xFFFF'FFFF'FFFF'FFFFULL;

  [[nodiscard]] constexpr auto is_null() const noexcept -> bool {
    return value == 0xFFFF'FFFF'FFFF'FFFFULL;
  }

  friend constexpr auto operator==(EntityHandle, EntityHandle) noexcept
      -> bool = default;
};

inline constexpr EntityHandle kNullEntityHandle{};

}  // namespace tess

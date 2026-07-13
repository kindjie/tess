#pragma once

#include <cstdint>

// EntityHandle lives in its own header so non-ECS layers (the render
// delta bridge in sim/delta_frame.h) can name entity identity without
// pulling in the agent pipeline; <tess/ecs/adapter.h> re-exports it.
namespace tess {

/**
 * Opaque, comparable entity identity shared across ECS integrations.
 *
 * Adapters preserve any native generation bits in `value`; tess interprets
 * only the distinguished null representation.
 */
struct EntityHandle {
  std::uint64_t value = 0xFFFF'FFFF'FFFF'FFFFULL;

  [[nodiscard]] constexpr auto is_null() const noexcept -> bool {
    return value == 0xFFFF'FFFF'FFFF'FFFFULL;
  }

  friend constexpr auto operator==(EntityHandle, EntityHandle) noexcept
      -> bool = default;
};

/** Null entity identity used for empty slots and failed lookups. */
inline constexpr EntityHandle kNullEntityHandle{};

}  // namespace tess

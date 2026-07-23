#pragma once

// Reference Dear ImGui world inspection and edit-intent helpers.
//
// This header is opt-in and dependency-free for tess core: its body is
// compiled only when the CONSUMER defines TESS_ENABLE_IMGUI on its own target.
// The consumer must include <imgui.h> BEFORE this header. tess.h does not
// include it, and tess never fetches or links Dear ImGui.
//
// The helpers accept const worlds. A widget edit returns an explicit intent;
// it never changes fields, residency, versions, or dirty state. The caller is
// responsible for validating and applying an intent at an appropriate phase.

#if defined(TESS_ENABLE_IMGUI)

#ifndef IMGUI_VERSION
#error "tess/debug/imgui/tools.h requires <imgui.h> to be included first"
#endif

#include <tess/storage/sparse_world.h>

#include <cstdint>
#include <optional>
#include <type_traits>

namespace tess::debug::imgui {

/** Outcome of inspecting a caller-selected tile or its owning chunk. */
enum class ToolStatus : std::uint8_t {
  Shown,
  OutOfBounds,
  Missing,
};

/** Caller-applied request to replace one boolean field value. */
struct BoolFieldEditIntent {
  Coord3 tile{};
  bool value = false;
};

/** Result of drawing a boolean field editor for one selected tile. */
struct BoolFieldEditResult {
  ToolStatus status = ToolStatus::Shown;
  std::optional<BoolFieldEditIntent> intent;
};

namespace detail {

[[nodiscard]] inline auto tool_to_ull(std::uint64_t value) noexcept
    -> unsigned long long {
  return static_cast<unsigned long long>(value);
}

[[nodiscard]] inline auto tool_to_ll(std::int64_t value) noexcept -> long long {
  return static_cast<long long>(value);
}

[[nodiscard]] inline auto tool_chunk_state_name(ChunkState state) noexcept
    -> const char* {
  switch (state) {
    case ChunkState::ResidentSleeping:
      return "sleeping";
    case ChunkState::ResidentActive:
      return "active";
  }
  return "?";
}

}  // namespace detail

/** Draws bounded shape, chunk-residency, and page-storage summary data. */
template <typename World>
void draw_world_overview(const World& world) {
  using Shape = typename World::shape_type;
  using Traits = ShapeTraits<Shape>;

  ImGui::TextUnformatted("World overview");
  ImGui::Separator();
  ImGui::Text("tiles: %llu x %llu x %llu", detail::tool_to_ull(Traits::size.x),
              detail::tool_to_ull(Traits::size.y),
              detail::tool_to_ull(Traits::size.z));
  ImGui::Text("chunks: %llu x %llu x %llu (%llu total)",
              detail::tool_to_ull(Traits::chunk_count_x),
              detail::tool_to_ull(Traits::chunk_count_y),
              detail::tool_to_ull(Traits::chunk_count_z),
              detail::tool_to_ull(World::chunk_count));
  ImGui::Text("page bytes: %llu", detail::tool_to_ull(World::page_byte_size));

  if constexpr (std::is_same_v<typename World::residency_type,
                               AlwaysResident>) {
    ImGui::Text("resident: %llu / %llu (%llu bytes)",
                detail::tool_to_ull(World::chunk_count),
                detail::tool_to_ull(World::chunk_count),
                detail::tool_to_ull(World::storage_byte_size));
  } else {
    ImGui::Text("resident: %llu / %llu (%llu bytes)",
                detail::tool_to_ull(world.resident_count()),
                detail::tool_to_ull(world.capacity()),
                detail::tool_to_ull(world.resident_byte_size()));
  }
}

/** Draws metadata for the chunk containing `selected`. */
template <typename World>
[[nodiscard]] auto draw_chunk_inspector(const World& world, Coord3 selected)
    -> ToolStatus {
  using Shape = typename World::shape_type;
  const auto resolved = world.try_resolve(selected);
  if (!resolved.has_value()) {
    ImGui::TextUnformatted("Selection is outside the world");
    return ToolStatus::OutOfBounds;
  }

  const auto* page = world.try_chunk(resolved->chunk_key);
  if (page == nullptr) {
    ImGui::Text("Chunk %llu is not resident",
                detail::tool_to_ull(resolved->chunk_key.value));
    return ToolStatus::Missing;
  }

  const auto& meta = world.meta(resolved->chunk_key);
  const auto chunk = chunk_coord<Shape>(resolved->chunk_key);
  const auto local = local_coord<Shape>(selected);
  ImGui::TextUnformatted("Chunk inspector");
  ImGui::Separator();
  ImGui::Text("tile: %lld, %lld, %lld", detail::tool_to_ll(selected.x),
              detail::tool_to_ll(selected.y), detail::tool_to_ll(selected.z));
  ImGui::Text("chunk: %llu (%llu, %llu, %llu)",
              detail::tool_to_ull(resolved->chunk_key.value),
              detail::tool_to_ull(chunk.x), detail::tool_to_ull(chunk.y),
              detail::tool_to_ull(chunk.z));
  ImGui::Text("local: %llu (%llu, %llu, %llu)",
              detail::tool_to_ull(resolved->local_tile_id.value),
              detail::tool_to_ull(local.x), detail::tool_to_ull(local.y),
              detail::tool_to_ull(local.z));
  ImGui::Text("state: %s; version: %u; topology: %u",
              detail::tool_chunk_state_name(meta.state), meta.version,
              meta.topology_version);
  ImGui::Text("active / entities: %u / %u", meta.active_count,
              meta.entity_count);
  ImGui::Text("dirty / active flags: 0x%08x / 0x%08x",
              world.dirty_flags(resolved->chunk_key),
              world.active_flags(resolved->chunk_key));
  return ToolStatus::Shown;
}

/**
 * Draws a boolean field widget and returns a caller-applied edit intent.
 *
 * The world is const and is never changed. `Tag` must identify a boolean field
 * in the world's schema. Missing sparse chunks are reported but not loaded.
 */
template <typename Tag, typename World>
[[nodiscard]] auto draw_bool_field_editor(const World& world, Coord3 selected,
                                          const char* label)
    -> BoolFieldEditResult {
  static_assert(
      std::is_same_v<typename World::schema_type::template value_type<Tag>,
                     bool>,
      "draw_bool_field_editor requires a bool field");

  if (!world.try_resolve(selected).has_value()) {
    ImGui::TextUnformatted("Selection is outside the world");
    return {ToolStatus::OutOfBounds, std::nullopt};
  }
  const auto* stored = world.template try_field<Tag>(selected);
  if (stored == nullptr) {
    ImGui::TextUnformatted("Selected tile is not resident");
    return {ToolStatus::Missing, std::nullopt};
  }

  auto edited = *stored;
  if (!ImGui::Checkbox(label, &edited)) {
    return {ToolStatus::Shown, std::nullopt};
  }
  return {ToolStatus::Shown, BoolFieldEditIntent{selected, edited}};
}

}  // namespace tess::debug::imgui

#endif  // defined(TESS_ENABLE_IMGUI)

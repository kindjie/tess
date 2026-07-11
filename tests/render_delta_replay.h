#pragma once

#include <tess/tess.h>

#include <cstdint>
#include <map>
#include <optional>
#include <tuple>

// Test-only consumer model for the M11 DeltaFrame protocol: a shadow
// grid applied with invalidation semantics (re-reading the CURRENT world
// for covered tiles), a shadow entity->tile map fed by entity deltas,
// and the version contract enforced exactly as a real consumer would.
// The section-8 acceptance "delta replay matches projected state" is
// this type's matches_world. May allocate freely.
namespace tess_test {

enum class ReplayApplyStatus : std::uint8_t { Applied, NeedsBaseline };

template <typename World, typename... Fields>
class RenderReplayGrid {
 public:
  using TileKey = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
  using TileValues = std::tuple<decltype(Fields{}, std::uint64_t{})...>;

  auto apply(const tess::DeltaFrame& frame, const World& world)
      -> ReplayApplyStatus {
    if (!tess::delta_frame_applicable(frame.header, version_)) {
      return ReplayApplyStatus::NeedsBaseline;
    }
    if (frame.header.baseline) {
      // A real consumer re-snapshots its entity presentation on every
      // baseline; the validator models that by clearing -- the test
      // reseeds via snapshot_entities from ground truth.
      shadow_.clear();
      entities_.clear();
    }
    for (const auto& chunk : frame.chunks) {
      if (chunk.tile_count != 0) {
        for (std::uint32_t i = 0; i < chunk.tile_count; ++i) {
          repaint(frame.tiles[chunk.first_tile + i].coord, world);
        }
      } else {
        const auto& box = chunk.bounds;
        const auto end_x =
            box.origin.x + static_cast<std::int64_t>(box.extent.x);
        const auto end_y =
            box.origin.y + static_cast<std::int64_t>(box.extent.y);
        const auto end_z =
            box.origin.z + static_cast<std::int64_t>(box.extent.z);
        for (auto z = box.origin.z; z < end_z; ++z) {
          for (auto y = box.origin.y; y < end_y; ++y) {
            for (auto x = box.origin.x; x < end_x; ++x) {
              repaint(tess::Coord3{x, y, z}, world);
            }
          }
        }
      }
    }
    for (const auto& record : frame.entities) {
      switch (record.kind) {
        case tess::EntityDeltaKind::Moved:
        case tess::EntityDeltaKind::Teleported:
        case tess::EntityDeltaKind::Spawned:
        case tess::EntityDeltaKind::Placed:
          entities_[record.entity.value] = record.to;
          break;
        case tess::EntityDeltaKind::Despawned:
        case tess::EntityDeltaKind::Parked:
          entities_.erase(record.entity.value);
          break;
      }
    }
    version_ = frame.header.to_version;
    return ReplayApplyStatus::Applied;
  }

  // Reseeds the entity map after a baseline apply, modeling the
  // consumer's ECS re-snapshot.
  void snapshot_entity(tess::EntityHandle entity, tess::Coord3 tile) {
    entities_[entity.value] = tile;
  }

  // Shadow == full fresh projection of Fields... over the world. Dense
  // worlds compare every tile; sparse worlds compare resident chunks
  // only. Meaningful once a baseline has been applied (full coverage).
  [[nodiscard]] auto matches_world(const World& world) const -> bool {
    using Shape = typename World::shape_type;
    const auto check_chunk = [&](tess::ChunkKey key) {
      using Traits = tess::ShapeTraits<Shape>;
      const auto chunk = tess::chunk_coord<Shape>(key);
      const auto origin =
          tess::Coord3{static_cast<std::int64_t>(chunk.x * Traits::chunk.x),
                       static_cast<std::int64_t>(chunk.y * Traits::chunk.y),
                       static_cast<std::int64_t>(chunk.z * Traits::chunk.z)};
      for (std::uint64_t z = 0; z < Traits::chunk.z; ++z) {
        for (std::uint64_t y = 0; y < Traits::chunk.y; ++y) {
          for (std::uint64_t x = 0; x < Traits::chunk.x; ++x) {
            const auto coord =
                tess::Coord3{origin.x + static_cast<std::int64_t>(x),
                             origin.y + static_cast<std::int64_t>(y),
                             origin.z + static_cast<std::int64_t>(z)};
            const auto it = shadow_.find(TileKey{coord.x, coord.y, coord.z});
            if (it == shadow_.end() || it->second != read(coord, world)) {
              return false;
            }
          }
        }
      }
      return true;
    };
    if constexpr (std::is_same_v<typename World::residency_type,
                                 tess::AlwaysResident>) {
      for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
        if (!check_chunk(tess::ChunkKey{key})) {
          return false;
        }
      }
    } else {
      for (const auto key : world.resident_chunk_keys()) {
        if (!check_chunk(key)) {
          return false;
        }
      }
    }
    return true;
  }

  [[nodiscard]] auto entity_tile(tess::EntityHandle entity) const
      -> std::optional<tess::Coord3> {
    const auto it = entities_.find(entity.value);
    if (it == entities_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  [[nodiscard]] auto entity_count() const -> std::size_t {
    return entities_.size();
  }

  [[nodiscard]] auto version() const noexcept -> tess::RenderVersion {
    return version_;
  }

 private:
  [[nodiscard]] auto read(tess::Coord3 coord, const World& world) const
      -> TileValues {
    return TileValues{
        static_cast<std::uint64_t>(world.template field<Fields>(coord))...};
  }

  void repaint(tess::Coord3 coord, const World& world) {
    shadow_[TileKey{coord.x, coord.y, coord.z}] = read(coord, world);
  }

  std::map<TileKey, TileValues> shadow_;
  std::map<std::uint64_t, tess::Coord3> entities_;
  tess::RenderVersion version_{0};  // fresh consumer: baseline-only entry
};

}  // namespace tess_test

// Headless DeltaFrame consumer: the living documentation of the M11
// frame contract. A small world publishes versioned frames of tile
// invalidations and entity deltas; the consumer applies them into its
// own shadow grid, deliberately drops one frame mid-run, detects the
// version gap through delta_frame_applicable, and resyncs with a full
// baseline. Prints the shadow after every applied frame.
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>

namespace {

struct GlyphTag {};

using Shape = tess::Shape<tess::Extent3{16, 8, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<GlyphTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

constexpr std::uint32_t kGlyphBit = 1U << 0U;

void paint(World& world, tess::Coord3 coord, char glyph) {
  world.field<GlyphTag>(coord) = static_cast<std::uint8_t>(glyph);
  world.mark_dirty(tess::chunk_key<Shape>(tess::tile_key<Shape>(coord)),
                   kGlyphBit, tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

// The consumer: a shadow grid, an entity marker map, and the echoed
// version. Everything a real renderer needs from the frame contract.
struct Consumer {
  std::array<std::array<char, 16>, 8> shadow{};
  std::map<std::uint64_t, tess::Coord3> markers;
  tess::RenderVersion version{};  // {0}: only a baseline can seed us

  Consumer() {
    for (auto& row : shadow) {
      row.fill('.');
    }
  }

  // Applies one frame with invalidation semantics: covered tiles are
  // re-read from the CURRENT world. Returns false when the version
  // chain does not match -- the caller must publish a baseline.
  auto apply(const tess::DeltaFrame& frame, const World& world) -> bool {
    if (!tess::delta_frame_applicable(frame.header, version)) {
      return false;
    }
    if (frame.header.baseline) {
      markers.clear();  // re-snapshot entity presentation on baselines
    }
    const auto repaint = [&](tess::Coord3 coord) {
      const auto value = world.field<GlyphTag>(coord);
      shadow[static_cast<std::size_t>(coord.y)]
            [static_cast<std::size_t>(coord.x)] =
                value == 0 ? '.' : static_cast<char>(value);
    };
    for (const auto& chunk : frame.chunks) {
      if (chunk.tile_count != 0) {
        for (std::uint32_t i = 0; i < chunk.tile_count; ++i) {
          repaint(frame.tiles[chunk.first_tile + i].coord);
        }
      } else {
        const auto& box = chunk.bounds;
        for (auto y = box.origin.y;
             y < box.origin.y + static_cast<std::int64_t>(box.extent.y); ++y) {
          for (auto x = box.origin.x;
               x < box.origin.x + static_cast<std::int64_t>(box.extent.x);
               ++x) {
            repaint(tess::Coord3{x, y, 0});
          }
        }
      }
    }
    for (const auto& record : frame.entities) {
      if (record.kind == tess::EntityDeltaKind::Despawned ||
          record.kind == tess::EntityDeltaKind::Parked) {
        markers.erase(record.entity.value);
      } else {
        markers[record.entity.value] = record.to;
      }
    }
    version = frame.header.to_version;
    return true;
  }

  void print() const {
    auto view = shadow;
    for (const auto& [value, coord] : markers) {
      view[static_cast<std::size_t>(coord.y)]
          [static_cast<std::size_t>(coord.x)] = '@';
    }
    for (const auto& row : view) {
      std::cout << "  ";
      std::cout.write(row.data(), static_cast<std::streamsize>(row.size()));
      std::cout << "\n";
    }
  }
};

auto run() -> int {
  World world;
  tess::DeltaCollector collector;
  collector.reserve(World::chunk_count, 256, 8);
  Consumer consumer;

  // Late join: the first delta frame is rejected, the baseline seeds us.
  paint(world, tess::Coord3{1, 1, 0}, '#');
  tess::collect_tile_deltas(collector, world, kGlyphBit);
  if (consumer.apply(collector.publish(), world)) {
    std::cerr << "a fresh consumer must not accept a delta frame\n";
    return 1;
  }
  std::cout << "frame rejected (fresh consumer) -> baseline\n";
  tess::collect_baseline(collector, world, kGlyphBit);
  if (!consumer.apply(collector.publish(), world)) {
    std::cerr << "baseline must always apply\n";
    return 1;
  }
  consumer.print();

  auto entity_tile = tess::Coord3{2, 2, 0};
  for (std::uint64_t tick = 1; tick <= 6; ++tick) {
    // Sim work: one wall painted, the marker entity walks right.
    collector.begin_tick(tick);
    paint(world,
          tess::Coord3{static_cast<std::int64_t>(3 + tick),
                       static_cast<std::int64_t>(tick % 8), 0},
          '#');
    const auto from = entity_tile;
    entity_tile.x += 1;
    collector.record_move(tess::EntityHandle{7}, from, entity_tile);
    tess::collect_tile_deltas(collector, world, kGlyphBit);
    const auto frame = collector.publish();

    if (tick == 3) {
      // The consumer misses this frame (window minimized, packet lost,
      // ...). Its state is now version-gapped.
      std::cout << "tick 3: frame dropped by the consumer\n";
      continue;
    }
    if (!consumer.apply(frame, world)) {
      std::cout << "tick " << tick << ": gap detected -> baseline resync\n";
      tess::collect_baseline(collector, world, kGlyphBit);
      if (!consumer.apply(collector.publish(), world)) {
        std::cerr << "baseline resync failed\n";
        return 1;
      }
      // Baselines cover tiles only: entity presentation re-snapshots
      // from the simulation's current truth.
      consumer.markers[7] = entity_tile;
    }
    std::cout << "tick " << tick << " (version " << consumer.version.value
              << "):\n";
    consumer.print();
  }
  std::cout << "consumer converged at version " << consumer.version.value
            << "\n";
  return 0;
}

}  // namespace

auto main() -> int {
  try {
    return run();
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}

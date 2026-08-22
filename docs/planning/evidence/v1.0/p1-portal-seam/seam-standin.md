# P1 seam stand-in: the measured scaffolding

The stand-in is not a merged change and not a proposed implementation. It
exists only to measure the ceiling a perfect seam index could reach, so it
deliberately omits everything a real index would owe: invalidation, validity
checks on either chunk, and any memory bound. It is correct only because both
screened workloads hold topology static.

Recorded as source rather than as a diff because a unified diff carries blank
context lines that the repository's whitespace check rejects. Two blocks were
inserted into `include/tess/path/path.h` at merged `main` 11cf6428, guarded by
`TESS_P1_SEAM_STANDIN`, and both are reproduced verbatim below.

Block one goes immediately before `best_chunk_portal`. Block two goes inside
that function's page-hoisted fast path, directly after `from_page` and
`to_page` are acquired and checked non-null, ahead of the existing axis loops.

```cpp
#ifdef TESS_P1_SEAM_STANDIN
// P1 screen scaffolding, never merged. Reproduces the authoritative seam
// iteration order of the page-hoisted fast path so the stand-in's tie
// breaking is identical to the incumbent's.
template <typename Shape, typename Visit>
void p1_walk_seam(ChunkCoord3 from, ChunkCoord3 to, Coord3 origin,
                  Visit&& visit) {
  using Traits = ShapeTraits<Shape>;
  constexpr auto chunk = Traits::chunk;
  if (from.x != to.x) {
    const auto step = from.x < to.x ? std::int64_t{1} : std::int64_t{-1};
    const auto source_x =
        step > 0 ? static_cast<std::int64_t>(chunk.x) - 1 : std::int64_t{0};
    const auto target_x =
        step > 0 ? std::int64_t{0} : static_cast<std::int64_t>(chunk.x) - 1;
    const auto world_target_x = origin.x + source_x + step;
    for (std::int64_t z = 0; z < static_cast<std::int64_t>(chunk.z); ++z) {
      for (std::int64_t y = 0; y < static_cast<std::int64_t>(chunk.y); ++y) {
        visit(LocalCoord3{static_cast<std::uint64_t>(source_x),
                          static_cast<std::uint64_t>(y),
                          static_cast<std::uint64_t>(z)},
              LocalCoord3{static_cast<std::uint64_t>(target_x),
                          static_cast<std::uint64_t>(y),
                          static_cast<std::uint64_t>(z)},
              Coord3{world_target_x, origin.y + y, origin.z + z});
      }
    }
    return;
  }
  if (from.y != to.y) {
    const auto step = from.y < to.y ? std::int64_t{1} : std::int64_t{-1};
    const auto source_y =
        step > 0 ? static_cast<std::int64_t>(chunk.y) - 1 : std::int64_t{0};
    const auto target_y =
        step > 0 ? std::int64_t{0} : static_cast<std::int64_t>(chunk.y) - 1;
    const auto world_target_y = origin.y + source_y + step;
    for (std::int64_t z = 0; z < static_cast<std::int64_t>(chunk.z); ++z) {
      for (std::int64_t x = 0; x < static_cast<std::int64_t>(chunk.x); ++x) {
        visit(LocalCoord3{static_cast<std::uint64_t>(x),
                          static_cast<std::uint64_t>(source_y),
                          static_cast<std::uint64_t>(z)},
              LocalCoord3{static_cast<std::uint64_t>(x),
                          static_cast<std::uint64_t>(target_y),
                          static_cast<std::uint64_t>(z)},
              Coord3{origin.x + x, world_target_y, origin.z + z});
      }
    }
    return;
  }
  const auto step = from.z < to.z ? std::int64_t{1} : std::int64_t{-1};
  const auto source_z =
      step > 0 ? static_cast<std::int64_t>(chunk.z) - 1 : std::int64_t{0};
  const auto target_z =
      step > 0 ? std::int64_t{0} : static_cast<std::int64_t>(chunk.z) - 1;
  const auto world_target_z = origin.z + source_z + step;
  for (std::int64_t y = 0; y < static_cast<std::int64_t>(chunk.y); ++y) {
    for (std::int64_t x = 0; x < static_cast<std::int64_t>(chunk.x); ++x) {
      visit(LocalCoord3{static_cast<std::uint64_t>(x),
                        static_cast<std::uint64_t>(y),
                        static_cast<std::uint64_t>(source_z)},
            LocalCoord3{static_cast<std::uint64_t>(x),
                        static_cast<std::uint64_t>(y),
                        static_cast<std::uint64_t>(target_z)},
            Coord3{origin.x + x, origin.y + y, world_target_z});
    }
  }
}
#endif

#ifdef TESS_P1_SEAM_STANDIN
      // P1 screen scaffolding, never merged. Measures the ceiling a
      // perfect seam index could reach: a flat table keyed by (from
      // chunk, signed step) holding the passable seam targets in the
      // authoritative scan order, with no invalidation, no validity
      // check, and no memory discipline. Everything a real index must
      // pay is omitted deliberately, so this bounds the real thing from
      // above. Correct only because the screened workloads hold
      // topology static.
      {
        struct P1Entry {
          std::vector<Coord3> targets;
          std::size_t scan_tiles = 0;
          bool valid = false;
        };
        static thread_local std::vector<P1Entry> p1_table;
        static thread_local const void* p1_owner = nullptr;
        constexpr auto p1_chunks = static_cast<std::size_t>(
            Traits::chunk_count_x * Traits::chunk_count_y *
            Traits::chunk_count_z);
        // Benchmark cells sharing one World type run sequentially against
        // different world instances; without this guard the second cell
        // would read the first cell's seam data. The identity check is
        // what makes the counter-identity gate meaningful.
        if (p1_owner != static_cast<const void*>(&world)) {
          p1_table.clear();
          p1_owner = static_cast<const void*>(&world);
        }
        if (p1_table.empty()) {
          p1_table.resize(p1_chunks * 6);
        }
        const auto p1_step = portal_step_code(from, to);
        const auto p1_slot =
            static_cast<std::size_t>(p1_step > 0 ? (p1_step - 1) * 2
                                                 : (-p1_step - 1) * 2 + 1);
        const auto p1_chunk_index =
            static_cast<std::size_t>(from.x) +
            static_cast<std::size_t>(from.y) * Traits::chunk_count_x +
            static_cast<std::size_t>(from.z) * Traits::chunk_count_x *
                Traits::chunk_count_y;
        auto& p1_entry = p1_table[p1_chunk_index * 6 + p1_slot];
        if (!p1_entry.valid) {
          const auto p1_record = [&](LocalCoord3 source_local,
                                     LocalCoord3 target_local, Coord3 target) {
            ++p1_entry.scan_tiles;
            if (!Class::passable(*from_page, local_tile_id<Shape>(source_local))) {
              return;
            }
            if (!Class::passable(*to_page, local_tile_id<Shape>(target_local))) {
              return;
            }
            p1_entry.targets.push_back(target);
          };
          p1_walk_seam<Shape>(from, to, origin, p1_record);
          p1_entry.valid = true;
        }
        if (scan_tiles != nullptr) {
          *scan_tiles += p1_entry.scan_tiles;
        }
        for (const auto target : p1_entry.targets) {
          score_target(target);
        }
        return found;
      }
#endif

```

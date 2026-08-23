# P2 measurement programs: recorded source

Three standalone programs, each compiled against the prototype in
`prototype.md` on main `e235fe05`. Exact invocations are in `README.md`;
captured outputs are alongside as `identity.txt`, `gates.txt`, and
`value.txt`.

## p2_identity.cc

```cpp
#include <tess/tess.h>
#include <cstdio>
#include <vector>
#include <string>
namespace {
struct PassableTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;
using Shape = tess::Shape<tess::Extent3{64,64,1}, tess::Extent3{16,16,1}>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

// SplitMix64, matching the fixture convention.
struct Rng {
  std::uint64_t s;
  auto next() -> std::uint64_t {
    s += 0x9E3779B97F4A7C15ULL; std::uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  auto below(std::uint64_t b) -> std::uint64_t { return ((next() >> 32) * b) >> 32; }
};

// Canonical fast-path-defeating overlay (tests/path_test_util.h recipe):
// two parallel walls, two adjacent gaps each, at opposite ends.
void add_serpentine(World& w) {
  for (int y = 0; y < 64; ++y) {
    if (y != 62 && y != 63) w.field<PassableTag>(tess::Coord3{30,y,0}) = false;
    if (y != 0 && y != 1) w.field<PassableTag>(tess::Coord3{34,y,0}) = false;
  }
}

auto build(World& w, std::uint64_t seed) {
  for (auto& page : w.chunks()) {
    auto open = page.template field_span<PassableTag>();
    for (std::size_t i = 0; i < open.size(); ++i) open[i] = true;
  }
  Rng rng{seed};
  // 25% obstacles, leaving a border lane so start/goal stay connected.
  for (int y = 1; y < 63; ++y)
    for (int x = 1; x < 63; ++x)
      if (rng.below(100) < 25) w.field<PassableTag>(tess::Coord3{x,y,0}) = false;
  for (int i = 0; i < 64; ++i) {
    w.field<PassableTag>(tess::Coord3{i,0,0}) = true;
    w.field<PassableTag>(tess::Coord3{0,i,0}) = true;
    w.field<PassableTag>(tess::Coord3{i,63,0}) = true;
    w.field<PassableTag>(tess::Coord3{63,i,0}) = true;
  }
}

auto contiguous(const World& w, tess::PathRequest req,
                std::size_t& expansions) -> std::vector<tess::Coord3> {
  tess::PathScratch scratch;
  const auto r = tess::astar_path<World, PassableTag>(
      w, req, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
  expansions = r.expanded_nodes;
  return std::vector<tess::Coord3>(r.path.begin(), r.path.end());
}

auto sliced(const World& w, tess::PathRequest req, std::size_t budget,
            std::uint64_t sched_seed, std::size_t& expansions,
            std::size_t& slices) -> std::vector<tess::Coord3> {
  tess::PathScratch scratch;
  tess::P2Slice slice;
  scratch.p2_slice_ = &slice;
  Rng rng{sched_seed};
  for (int guard = 0; guard < 2000000; ++guard) {
    slice.budget = budget ? budget : (1 + rng.below(32));
    const auto r = tess::astar_path<World, PassableTag>(
        w, req, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
    if (r.status != tess::PathStatus::Indeterminate || slice.budget == 0) {
      expansions = r.expanded_nodes; slices = slice.slices;
      return std::vector<tess::Coord3>(r.path.begin(), r.path.end());
    }
    // Indeterminate with a live frontier means the slice budget ran out.
    if (!slice.paused) {
      expansions = r.expanded_nodes; slices = slice.slices;
      return std::vector<tess::Coord3>(r.path.begin(), r.path.end());
    }
  }
  expansions = 0; slices = 0; return {};
}
}  // namespace

int main() {
  int checked = 0, route_mismatch = 0, exp_mismatch = 0;
  int paused_runs = 0, atomic_runs = 0, never_sliced_serp = 0;
  // Family A: random maps (preamble handling; fast paths may answer in the
  // atomic slice 0, which is the declared scope limit, not a defect).
  // Family B: the same maps with the canonical serpentine overlay, which
  // defeats every fast path, so every k>0 run MUST actually pause -- the
  // earlier revision of this probe never checked that and could have
  // reported vacuous identity over preamble-answered runs.
  for (int family = 0; family < 2; ++family) {
    for (unsigned trial = 0; trial < 20; ++trial) {
      World w;
      if (family == 0) {
        build(w, 0x9E3779B97F4A7C15ULL * (trial + 1));
      } else {
        // Clean map + serpentine: deterministic connectivity, and only the
        // heap loop can answer it. Random obstacles on top disconnect the
        // two-wall corridor too often to leave a usable family.
        for (auto& page : w.chunks()) {
          auto open = page.template field_span<PassableTag>();
          for (std::size_t i = 0; i < open.size(); ++i) open[i] = true;
        }
        add_serpentine(w);
        if (trial > 0) break;  // one map; the schedule loop still varies k.
      }
      tess::PathRequest req{tess::Coord3{1,1,0}, tess::Coord3{62,62,0}};
      std::size_t base_exp = 0;
      const auto base = contiguous(w, req, base_exp);
      if (base.empty()) continue;
      for (std::size_t k : {std::size_t{1}, std::size_t{8}, std::size_t{64},
                            std::size_t{0}}) {
        std::size_t exp = 0, slices = 0;
        const auto got = sliced(w, req, k, 0x1234567 + trial, exp, slices);
        ++checked;
        if (slices > 1) ++paused_runs; else ++atomic_runs;
        if (family == 1 && k == 1 && slices <= 1) {
          ++never_sliced_serp;
          std::printf("NEVER SLICED (serp) trial=%u k=%zu\n", trial, k);
        }
        if (got != base) { ++route_mismatch;
          std::printf("ROUTE MISMATCH family=%d trial=%u k=%zu base=%zu got=%zu\n",
                      family, trial, k, base.size(), got.size()); }
        if (exp != base_exp) { ++exp_mismatch;
          std::printf("EXPANSION MISMATCH family=%d trial=%u k=%zu base=%zu got=%zu slices=%zu\n",
                      family, trial, k, base_exp, exp, slices); }
      }
    }
  }
  std::printf("\nchecked=%d route_mismatch=%d expansion_mismatch=%d "
              "paused_runs=%d atomic_runs=%d never_sliced_serp=%d\n",
              checked, route_mismatch, exp_mismatch, paused_runs,
              atomic_runs, never_sliced_serp);
  return route_mismatch + exp_mismatch + never_sliced_serp ? 1 : 0;
}
```

## p2_gates.cc

```cpp
// P2 gate probe: staleness, residency corruption, cancellation, misuse,
// memory, allocation. Runs against the prototype behind -DTESS_P2_RESUMABLE.
#include <tess/tess.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      ++failures;                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                 \
  } while (0)

struct PassableTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;

// ---- Dense fixtures: 64x64, 4x4 chunk grid of 16x16 chunks. ----
using DenseShape =
    tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;
using Dense = tess::AlwaysResidentWorld<DenseShape, Schema>;

void fill_dense(Dense& w) {
  for (auto& page : w.chunks()) {
    auto open = page.template field_span<PassableTag>();
    for (std::size_t i = 0; i < open.size(); ++i) open[i] = true;
  }
}

// The canonical fast-path-defeating recipe from tests/path_test_util.h:
// two parallel walls, each with TWO adjacent gaps (forced-plane-gaps bails
// on passable_count > 1), gaps at opposite ends, start/goal displaced on
// both non-degenerate axes. Only the real heap loop can answer it.
void add_walls(Dense& w) {
  for (int y = 0; y < 64; ++y) {
    if (y != 62 && y != 63) w.field<PassableTag>({30, y, 0}) = false;
    if (y != 0 && y != 1) w.field<PassableTag>({34, y, 0}) = false;
  }
}

template <typename W, typename Tag>
auto run_contiguous(const W& w, tess::PathRequest req)
    -> std::vector<tess::Coord3> {
  tess::PathScratch scratch;
  const auto r = tess::astar_path<W, Tag>(
      w, req, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
  return {r.path.begin(), r.path.end()};
}

// Runs sliced to completion; returns the route (empty on refusal/failure).
template <typename W, typename Tag>
auto run_sliced(const W& w, tess::PathRequest req, tess::PathScratch& scratch,
                tess::P2Slice& slice, std::size_t budget)
    -> std::vector<tess::Coord3> {
  scratch.p2_slice_ = &slice;
  slice.budget = budget;
  for (int guard = 0; guard < 2000000; ++guard) {
    const auto r = tess::astar_path<W, Tag>(
        w, req, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
    if (!slice.paused) return {r.path.begin(), r.path.end()};
  }
  return {};
}

// Advances a sliced query until the first pause; asserts it paused.
template <typename W, typename Tag>
void pause_once(const W& w, tess::PathRequest req, tess::PathScratch& scratch,
                tess::P2Slice& slice, std::size_t budget) {
  scratch.p2_slice_ = &slice;
  slice.budget = budget;
  const auto r = tess::astar_path<W, Tag>(
      w, req, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
  CHECK(r.status == tess::PathStatus::Indeterminate);
  CHECK(slice.paused);
  CHECK(!slice.stale);
}

// One more slice after a pause; reports the result.
template <typename W, typename Tag>
auto resume_once(const W& w, tess::PathRequest req, tess::PathScratch& scratch)
    -> tess::PathResult {
  return tess::astar_path<W, Tag>(
      w, req, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
}

bool captured(const tess::P2Slice& slice, tess::ChunkKey key) {
  for (const auto& dep : slice.deps)
    if (dep.key == key) return true;
  return false;
}

void dense_staleness() {
  Dense w;
  fill_dense(w);
  add_walls(w);
  const tess::PathRequest req{{1, 1, 0}, {62, 62, 0}};
  const auto base = run_contiguous<Dense, PassableTag>(w, req);
  CHECK(!base.empty());

  // D1: version-marked edit of a captured chunk between slices -> stale.
  {
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Dense, PassableTag>(w, req, scratch, slice, 8);
    const auto touched_key = slice.deps.at(0).key;
    // The edit itself: flip one tile and mark it. Restored afterward.
    const auto coord = tess::coord<DenseShape>(
        tess::chunk_coord<DenseShape>(touched_key), tess::LocalTileId{0});
    const bool old = w.field<PassableTag>(coord);
    w.field<PassableTag>(coord) = !old;
    w.mark_content_changed(touched_key);
    const auto r = resume_once<Dense, PassableTag>(w, req, scratch);
    CHECK(r.status == tess::PathStatus::Indeterminate);
    CHECK(slice.stale);
    CHECK(!slice.paused);
    CHECK(r.path.empty());
    // Still stale on a further call; reset() then rerun succeeds.
    const auto r2 = resume_once<Dense, PassableTag>(w, req, scratch);
    CHECK(slice.stale && r2.path.empty());
    w.field<PassableTag>(coord) = old;
    w.mark_content_changed(touched_key);
    slice.reset();
    const auto again = run_sliced<Dense, PassableTag>(w, req, scratch, slice, 8);
    CHECK(again == base);
  }

  // D2: version-marked edit of an UNCAPTURED chunk -> resume completes with
  // the original route (chunk granularity, no false positive). Requires a
  // chunk the search never read; verify it is absent from deps first.
  {
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Dense, PassableTag>(w, req, scratch, slice, 8);
    tess::ChunkKey untouched{};
    bool found = false;
    for (std::uint64_t k = 0; k < 16; ++k) {
      if (!captured(slice, tess::ChunkKey{k})) {
        untouched = tess::ChunkKey{k};
        found = true;
        break;
      }
    }
    if (found) {
      w.mark_content_changed(untouched);
      const auto rest = [&] {
        for (int guard = 0; guard < 2000000; ++guard) {
          const auto r = resume_once<Dense, PassableTag>(w, req, scratch);
          if (!slice.paused)
            return std::vector<tess::Coord3>(r.path.begin(), r.path.end());
        }
        return std::vector<tess::Coord3>{};
      }();
      CHECK(!slice.stale);
      CHECK(rest == base);
    } else {
      // At k=8 after one slice, a 16-chunk map with every chunk already
      // captured would make this arm vacuous; report rather than pass.
      std::printf("NOTE: D2 vacuous, all 16 chunks captured on slice 0\n");
    }
  }

  // D3: raw field write WITHOUT mark_content_changed is declared
  // undetectable; demonstrate the boundary honestly: resume completes.
  {
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Dense, PassableTag>(w, req, scratch, slice, 8);
    const auto coord0 = tess::coord<DenseShape>(
        tess::chunk_coord<DenseShape>(slice.deps.at(0).key),
        tess::LocalTileId{0});
    const bool old = w.field<PassableTag>(coord0);
    w.field<PassableTag>(coord0) = !old;  // no mark: invisible by contract
    const auto r = resume_once<Dense, PassableTag>(w, req, scratch);
    CHECK(!slice.stale);
    (void)r;
    w.field<PassableTag>(coord0) = old;
  }

  // D4: misuse -- resuming with a different request. In assert-enabled
  // builds the TESS_ASSERT aborts (verified by running this probe without
  // NDEBUG and observing the abort); the refusal-and-recover contract below
  // is the release-build behavior, so it compiles only under NDEBUG.
#ifdef NDEBUG
  {
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Dense, PassableTag>(w, req, scratch, slice, 8);
    const tess::PathRequest other{{1, 62, 0}, {62, 1, 0}};
    const auto r = resume_once<Dense, PassableTag>(w, other, scratch);
    CHECK(r.status == tess::PathStatus::Indeterminate);
    CHECK(slice.stale);
    CHECK(r.path.empty());
    slice.reset();
    const auto fresh = run_sliced<Dense, PassableTag>(w, other, scratch, slice, 8);
    CHECK((fresh == run_contiguous<Dense, PassableTag>(w, other)));
  }
#endif

  // D5: cancellation -- pause query A, reset, run query B sliced on the SAME
  // state object and scratch; then A again from scratch. Both must match
  // their contiguous results.
  {
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Dense, PassableTag>(w, req, scratch, slice, 1);
    slice.reset();
    const tess::PathRequest b{{62, 1, 0}, {1, 62, 0}};
    const auto got_b = run_sliced<Dense, PassableTag>(w, b, scratch, slice, 8);
    CHECK((got_b == run_contiguous<Dense, PassableTag>(w, b)));
    slice.reset();
    const auto got_a = run_sliced<Dense, PassableTag>(w, req, scratch, slice, 8);
    CHECK(got_a == base);
  }

  // D6: memory + allocation. Paused-state addition = deps bytes + P2Slice;
  // incumbent scratch for this world is capacity_hint nodes. Also: no deps
  // reallocation after slice 0 (cold reserve holds).
  {
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Dense, PassableTag>(w, req, scratch, slice, 8);
    const auto cap_after_slice0 = slice.deps.capacity();
    const auto data_after_slice0 =
        static_cast<const void*>(slice.deps.data());
    for (int guard = 0; guard < 2000000; ++guard) {
      const auto r = resume_once<Dense, PassableTag>(w, req, scratch);
      if (!slice.paused) {
        CHECK(std::vector<tess::Coord3>(r.path.begin(), r.path.end()) == base);
        break;
      }
    }
    CHECK(slice.deps.capacity() == cap_after_slice0);
    CHECK(static_cast<const void*>(slice.deps.data()) == data_after_slice0);
    std::printf(
        "dense memory: deps=%zu entries (%zu B) + sizeof(P2Slice)=%zu B; "
        "incumbent scratch nodes=%llu; slices=%zu capture_probes=%zu "
        "revalidation_checks=%zu\n",
        slice.deps.size(), slice.deps.size() * sizeof(tess::P2Slice::Dep),
        sizeof(tess::P2Slice),
        static_cast<unsigned long long>(DenseShape::size.x *
                                        DenseShape::size.y),
        slice.slices, slice.capture_probes, slice.revalidation_checks);
  }
}

// ---- Sparse fixtures: 96x32, three 32x32 chunks in a row. ----
using SparseShape =
    tess::Shape<tess::Extent3{96, 32, 1}, tess::Extent3{32, 32, 1}>;
using Sparse = tess::SparseResidentWorld<SparseShape, Schema>;

void fill_chunk(Sparse& world, tess::ChunkKey key) {
  world.ensure_resident(key);
  auto& page = world.chunk(key);
  for (std::uint64_t i = 0; i < Sparse::local_tile_count; ++i) {
    page.template field<PassableTag>(tess::LocalTileId{i}) = true;
  }
}

// Start on the chunk-0/1 boundary column so slice 0's first expansion
// deterministically captures chunk 1 (and, at (31,0), chunk 0's own key).
void sparse_staleness() {
  const tess::PathRequest req{{31, 0, 0}, {0, 31, 0}};

  // S1: evicting a captured chunk between slices -> stale on resume,
  // detected before any scratch read.
  {
    Sparse w{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
    fill_chunk(w, tess::ChunkKey{0});
    fill_chunk(w, tess::ChunkKey{1});
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Sparse, PassableTag>(w, req, scratch, slice, 1);
    CHECK(captured(slice, tess::ChunkKey{1}));
    CHECK(w.evict(tess::ChunkKey{1}));
    const auto r = resume_once<Sparse, PassableTag>(w, req, scratch);
    CHECK(r.status == tess::PathStatus::Indeterminate);
    CHECK(slice.stale);
    CHECK(r.path.empty());
  }

  // S2: slot aliasing -- evict a captured chunk and materialize a DIFFERENT
  // key into the freed slot. The incumbent invariant "slot mapping is fixed
  // for one search" is broken; the paused search must refuse, not read the
  // newcomer's tiles through stale offsets.
  {
    Sparse w{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
    fill_chunk(w, tess::ChunkKey{0});
    fill_chunk(w, tess::ChunkKey{1});
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Sparse, PassableTag>(w, req, scratch, slice, 1);
    CHECK(w.evict(tess::ChunkKey{1}));
    fill_chunk(w, tess::ChunkKey{2});  // takes the freed slot
    const auto r = resume_once<Sparse, PassableTag>(w, req, scratch);
    CHECK(slice.stale);
    CHECK(r.path.empty());
  }

  // S3: ABA rematerialization -- evict then re-materialize the SAME key.
  // Content version restarts at zero (equal to a fresh capture's zero would
  // be possible); the residency generation moved, which must be sufficient.
  {
    Sparse w{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
    fill_chunk(w, tess::ChunkKey{0});
    fill_chunk(w, tess::ChunkKey{1});
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Sparse, PassableTag>(w, req, scratch, slice, 1);
    CHECK(w.evict(tess::ChunkKey{1}));
    fill_chunk(w, tess::ChunkKey{1});
    const auto r = resume_once<Sparse, PassableTag>(w, req, scratch);
    CHECK(slice.stale);
    CHECK(r.path.empty());
  }

  // S4: a chunk that was NOT resident at capture (generation zero) becoming
  // resident between slices -> stale. The skipped-neighbor conclusion the
  // search already drew is no longer justified.
  {
    Sparse w{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
    fill_chunk(w, tess::ChunkKey{0});
    // Chunk 1 left non-resident; start sits on its boundary.
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Sparse, PassableTag>(w, req, scratch, slice, 1);
    CHECK(captured(slice, tess::ChunkKey{1}));
    fill_chunk(w, tess::ChunkKey{1});
    const auto r = resume_once<Sparse, PassableTag>(w, req, scratch);
    CHECK(slice.stale);
    CHECK(r.path.empty());
  }

  // S5: version-marked edit in a captured resident chunk -> stale.
  {
    Sparse w{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
    fill_chunk(w, tess::ChunkKey{0});
    fill_chunk(w, tess::ChunkKey{1});
    tess::PathScratch scratch;
    tess::P2Slice slice;
    pause_once<Sparse, PassableTag>(w, req, scratch, slice, 1);
    w.chunk(tess::ChunkKey{0})
        .template field<PassableTag>(tess::LocalTileId{5}) = false;
    w.mark_content_changed(tess::ChunkKey{0});
    const auto r = resume_once<Sparse, PassableTag>(w, req, scratch);
    CHECK(slice.stale);
    CHECK(r.path.empty());
  }

  // S6: no world change -> sliced completes and matches contiguous.
  {
    Sparse w{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
    fill_chunk(w, tess::ChunkKey{0});
    fill_chunk(w, tess::ChunkKey{1});
    const auto base = run_contiguous<Sparse, PassableTag>(w, req);
    CHECK(!base.empty());
    tess::PathScratch scratch;
    tess::P2Slice slice;
    const auto got = run_sliced<Sparse, PassableTag>(w, req, scratch, slice, 1);
    CHECK(got == base);
    CHECK(!slice.stale);
    std::printf(
        "sparse: deps=%zu slices=%zu capture_probes=%zu "
        "revalidation_checks=%zu\n",
        slice.deps.size(), slice.slices, slice.capture_probes,
        slice.revalidation_checks);
  }
}

}  // namespace

int main() {
  dense_staleness();
  sparse_staleness();
  if (failures == 0) std::printf("\nALL GATE CHECKS PASSED\n");
  return failures == 0 ? 0 : 1;
}
```

## p2_value.cc

```cpp
// P2 scheduling-value probe. Compares the pre-registered non-expansion work
// counters between the contiguous baseline (slice == nullptr) and sliced
// runs, under the amended accounting: the resumed arm's total additionally
// pays every capture probe and every revalidation check the prototype
// performs. Compile with -DTESS_P2_RESUMABLE -DTESS_ENABLE_DIAGNOSTICS.
#include <tess/tess.h>

#include <cstdio>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      ++failures;                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                 \
  } while (0)

struct PassableTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;

struct Rng {
  std::uint64_t s;
  auto next() -> std::uint64_t {
    s += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  auto below(std::uint64_t b) -> std::uint64_t {
    return ((next() >> 32) * b) >> 32;
  }
};

// The four pre-registered non-expansion counters.
std::uint64_t declared_total(const tess::diagnostics::PathCounters& c) {
  return c.passability_checks + c.heap_pushes + c.heap_pops +
         c.relax_attempts;
}

struct RunResult {
  std::uint64_t library = 0;        // declared_total over library counters
  std::uint64_t entry_checks = 0;   // start+goal checks (re-run per resume)
  std::uint64_t capture = 0;        // prototype capture probes
  std::uint64_t reval = 0;          // prototype revalidation checks
  std::size_t slices = 0;
  std::size_t deps = 0;
  std::size_t max_slice_expansions = 0;
  std::vector<tess::Coord3> route;
};

template <typename W>
RunResult run_contiguous(const W& w, tess::PathRequest req) {
  RunResult out;
  tess::diagnostics::PathCounters counters;
  tess::PathScratch scratch;
  {
    const tess::diagnostics::ScopedPathCounters scope{counters};
    const auto r = tess::astar_path<W, PassableTag>(
        w, req, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
    out.route.assign(r.path.begin(), r.path.end());
  }
  out.library = declared_total(counters);
  out.entry_checks =
      counters.start_passability_checks + counters.goal_passability_checks;
  out.slices = 1;
  return out;
}

template <typename W>
RunResult run_sliced(const W& w, tess::PathRequest req, std::size_t budget,
                     std::uint64_t sched_seed) {
  RunResult out;
  tess::diagnostics::PathCounters counters;
  tess::PathScratch scratch;
  tess::P2Slice slice;
  scratch.p2_slice_ = &slice;
  Rng rng{sched_seed};
  std::size_t prev_expanded = 0;
  {
    const tess::diagnostics::ScopedPathCounters scope{counters};
    for (int guard = 0; guard < 2000000; ++guard) {
      slice.budget = budget ? budget : (1 + rng.below(32));
      const auto r = tess::astar_path<W, PassableTag>(
          w, req, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
      const std::size_t this_slice = r.expanded_nodes - prev_expanded;
      prev_expanded = r.expanded_nodes;
      if (this_slice > out.max_slice_expansions) {
        out.max_slice_expansions = this_slice;
      }
      // The declared bound: a slice never expands more than its budget.
      if (budget != 0) CHECK(this_slice <= budget);
      if (!slice.paused) {
        out.route.assign(r.path.begin(), r.path.end());
        break;
      }
    }
  }
  out.library = declared_total(counters);
  out.entry_checks =
      counters.start_passability_checks + counters.goal_passability_checks;
  out.capture = slice.capture_probes;
  out.reval = slice.revalidation_checks;
  out.slices = slice.slices;
  out.deps = slice.deps.size();
  return out;
}

struct Agg {
  double max_growth = -1e9;
  double sum_growth = 0;
  int n = 0;
  void add(double g) {
    if (g > max_growth) max_growth = g;
    sum_growth += g;
    ++n;
  }
};

template <typename Shape>
void serpentine_family(const char* label) {
  using W = tess::AlwaysResidentWorld<Shape, Schema>;
  constexpr auto sx = static_cast<int>(Shape::size.x);
  constexpr auto sy = static_cast<int>(Shape::size.y);
  auto w = std::make_unique<W>();
  for (auto& page : w->chunks()) {
    auto open = page.template field_span<PassableTag>();
    for (std::size_t i = 0; i < open.size(); ++i) open[i] = true;
  }
  // Two-adjacent-gap walls at opposite ends (path_test_util.h recipe),
  // scaled: walls flank the vertical midline.
  const int wall_a = sx / 2 - 2;
  const int wall_b = sx / 2 + 2;
  for (int y = 0; y < sy; ++y) {
    if (y != sy - 2 && y != sy - 1)
      w->template field<PassableTag>({wall_a, y, 0}) = false;
    if (y != 0 && y != 1)
      w->template field<PassableTag>({wall_b, y, 0}) = false;
  }
  const tess::PathRequest req{{1, 1, 0}, {sx - 2, sy - 2, 0}};
  const auto base = run_contiguous<W>(*w, req);
  CHECK(!base.route.empty());
  for (std::size_t k : {std::size_t{1}, std::size_t{8}, std::size_t{64}}) {
    const auto run = run_sliced<W>(*w, req, k, 0);
    CHECK(run.route == base.route);
    const auto resumed_total = run.library + run.capture + run.reval;
    const double growth =
        static_cast<double>(resumed_total) /
            static_cast<double>(base.library) - 1.0;
    std::printf(
        "%s k=%-3zu chunks=%llu slices=%zu deps=%zu | base=%llu "
        "resumed=%llu (lib=%llu cap=%llu reval=%llu) growth=%+.2f%% | "
        "entry %llu->%llu | max_slice_exp=%zu\n",
        label, k, static_cast<unsigned long long>(W::chunk_count),
        run.slices, run.deps,
        static_cast<unsigned long long>(base.library),
        static_cast<unsigned long long>(resumed_total),
        static_cast<unsigned long long>(run.library),
        static_cast<unsigned long long>(run.capture),
        static_cast<unsigned long long>(run.reval), growth * 100.0,
        static_cast<unsigned long long>(base.entry_checks),
        static_cast<unsigned long long>(run.entry_checks),
        run.max_slice_expansions);
  }
}

void random_family() {
  using Shape = tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;
  using W = tess::AlwaysResidentWorld<Shape, Schema>;
  Agg agg[3];
  const std::size_t ks[3] = {1, 8, 64};
  int used = 0;
  for (unsigned trial = 0; trial < 20; ++trial) {
    auto w = std::make_unique<W>();
    for (auto& page : w->chunks()) {
      auto open = page.template field_span<PassableTag>();
      for (std::size_t i = 0; i < open.size(); ++i) open[i] = true;
    }
    Rng rng{0x9E3779B97F4A7C15ULL * (trial + 1)};
    for (int y = 1; y < 63; ++y)
      for (int x = 1; x < 63; ++x)
        if (rng.below(100) < 25)
          w->template field<PassableTag>({x, y, 0}) = false;
    for (int i = 0; i < 64; ++i) {
      w->template field<PassableTag>({i, 0, 0}) = true;
      w->template field<PassableTag>({0, i, 0}) = true;
      w->template field<PassableTag>({i, 63, 0}) = true;
      w->template field<PassableTag>({63, i, 0}) = true;
    }
    const tess::PathRequest req{{1, 1, 0}, {62, 62, 0}};
    const auto base = run_contiguous<W>(*w, req);
    if (base.route.empty()) continue;
    ++used;
    for (int i = 0; i < 3; ++i) {
      const auto run = run_sliced<W>(*w, req, ks[i], 0x1234567 + trial);
      CHECK(run.route == base.route);
      const auto resumed_total = run.library + run.capture + run.reval;
      agg[i].add(static_cast<double>(resumed_total) /
                     static_cast<double>(base.library) -
                 1.0);
    }
  }
  for (int i = 0; i < 3; ++i) {
    std::printf("random64 k=%-3zu trials=%d mean_growth=%+.2f%% "
                "max_growth=%+.2f%%\n",
                ks[i], used, 100.0 * agg[i].sum_growth / agg[i].n,
                100.0 * agg[i].max_growth);
  }
}

}  // namespace

int main() {
  std::printf("-- serpentine scaling (same recipe, growing map) --\n");
  serpentine_family<
      tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>>(
      "serp64 ");
  serpentine_family<
      tess::Shape<tess::Extent3{128, 128, 1}, tess::Extent3{16, 16, 1}>>(
      "serp128");
  serpentine_family<
      tess::Shape<tess::Extent3{256, 256, 1}, tess::Extent3{16, 16, 1}>>(
      "serp256");
  std::printf("-- random maps --\n");
  random_family();
  if (failures == 0) std::printf("\nALL VALUE CHECKS PASSED\n");
  return failures == 0 ? 0 : 1;
}
```

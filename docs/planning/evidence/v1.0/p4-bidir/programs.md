# P4 measurement programs: recorded source

Compiled against the prototype in `prototype.md` (which includes the
branch-only bench source). Captured outputs sit alongside.

## p4_correct.cc (correctness gates + oracle + ineligible-caller gate)

```cpp
// P4 correctness probe: cost exactness vs an independent BFS oracle,
// route validity, reachability agreement, determinism, and the
// ineligible-caller gate. Compile with -DTESS_P4_BIDIR.
#include <tess/tess.h>

#include <cstdio>
#include <deque>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, ...)                                              \
  do {                                                                \
    if (!(cond)) {                                                    \
      ++failures;                                                     \
      std::printf("FAIL line %d: %s ", __LINE__, #cond);              \
      std::printf(__VA_ARGS__);                                       \
      std::printf("\n");                                              \
    }                                                                 \
  } while (0)

struct PassableTag {};
struct CostTag {};
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

// Terrain families from the pre-registration. `grid[y*n+x] != 0` = open.
enum class FamilyKind { Open, WallGap, Maze, Rubble };
const char* family_name(FamilyKind f) {
  switch (f) {
    case FamilyKind::Open: return "open";
    case FamilyKind::WallGap: return "wall_gap";
    case FamilyKind::Maze: return "maze";
    case FamilyKind::Rubble: return "rubble";
  }
  return "?";
}

std::vector<std::uint8_t> build_terrain(FamilyKind family, int n,
                                        std::uint64_t seed) {
  std::vector<std::uint8_t> grid(static_cast<std::size_t>(n) * n, 1);
  const auto close = [&](int x, int y) {
    grid[static_cast<std::size_t>(y) * n + x] = 0;
  };
  switch (family) {
    case FamilyKind::Open:
      break;
    case FamilyKind::WallGap: {
      // Parallel vertical walls every n/8 columns, two-tile gaps at
      // alternating ends (the fast-path-defeating recipe, scaled).
      const int spacing = n / 8;
      int k = 0;
      for (int x = spacing; x < n - 1; x += spacing, ++k) {
        const bool gap_high = (k % 2) == 0;
        for (int y = 0; y < n; ++y) {
          const bool in_gap = gap_high ? (y >= n - 2) : (y <= 1);
          if (!in_gap) close(x, y);
        }
      }
      break;
    }
    case FamilyKind::Maze: {
      // Serpentine: horizontal walls every 4 rows, one two-tile gap per
      // wall at alternating ends.
      int k = 0;
      for (int y = 4; y < n - 1; y += 4, ++k) {
        const bool gap_right = (k % 2) == 0;
        for (int x = 0; x < n; ++x) {
          const bool in_gap = gap_right ? (x >= n - 2) : (x <= 1);
          if (!in_gap) close(x, y);
        }
      }
      break;
    }
    case FamilyKind::Rubble: {
      Rng rng{seed};
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
          if (rng.below(100) < 25) close(x, y);
      break;
    }
  }
  return grid;
}

// Independent oracle: plain BFS over the terrain grid.
int oracle_cost(const std::vector<std::uint8_t>& grid, int n, tess::Coord3 s,
                tess::Coord3 g) {
  if (grid[static_cast<std::size_t>(s.y) * n + s.x] == 0 ||
      grid[static_cast<std::size_t>(g.y) * n + g.x] == 0) {
    return -1;
  }
  std::vector<int> dist(static_cast<std::size_t>(n) * n, -1);
  std::deque<std::pair<int, int>> q{{static_cast<int>(s.x),
                                     static_cast<int>(s.y)}};
  dist[static_cast<std::size_t>(s.y) * n + s.x] = 0;
  while (!q.empty()) {
    const auto [x, y] = q.front();
    q.pop_front();
    const auto d = dist[static_cast<std::size_t>(y) * n + x];
    if (x == g.x && y == g.y) return d;
    const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& st : steps) {
      const int nx = x + st[0];
      const int ny = y + st[1];
      if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
      if (grid[static_cast<std::size_t>(ny) * n + nx] == 0) continue;
      auto& cell = dist[static_cast<std::size_t>(ny) * n + nx];
      if (cell != -1) continue;
      cell = d + 1;
      q.emplace_back(nx, ny);
    }
  }
  return -1;
}

template <typename World>
void fill_from_grid(World& w, const std::vector<std::uint8_t>& grid, int n) {
  for (auto& page : w.chunks()) {
    auto open = page.template field_span<PassableTag>();
    for (std::size_t i = 0; i < open.size(); ++i) open[i] = false;
  }
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x)
      if (grid[static_cast<std::size_t>(y) * n + x] != 0)
        w.template field<PassableTag>(tess::Coord3{x, y, 0}) = true;
}

template <typename World>
bool route_valid(const World& w, tess::PathRequest req,
                 std::span<const tess::Coord3> path, std::uint32_t cost) {
  if (path.empty()) return false;
  if (!(path.front() == req.start) || !(path.back() == req.goal)) return false;
  if (path.size() != static_cast<std::size_t>(cost) + 1) return false;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (!w.template field<PassableTag>(path[i])) return false;
    if (i > 0) {
      const auto dx = path[i].x - path[i - 1].x;
      const auto dy = path[i].y - path[i - 1].y;
      if ((dx * dx + dy * dy) != 1) return false;
    }
  }
  return true;
}

template <int N>
void run_family(FamilyKind family, int& checked, int& jps_served) {
  using Shape =
      tess::Shape<tess::Extent3{N, N, 1}, tess::Extent3{16, 16, 1}>;
  using World = tess::AlwaysResidentWorld<Shape, Schema>;
  auto w = std::make_unique<World>();
  for (unsigned trial = 0; trial < 20; ++trial) {
    const auto seed = 0x9E3779B97F4A7C15ULL *
                      (static_cast<std::uint64_t>(family) * 1000003ULL +
                       trial + 1);
    const auto grid = build_terrain(family, N, seed);
    fill_from_grid(*w, grid, N);
    // Start/goal from the free set by the same stream.
    Rng rng{seed ^ 0x5A5A5A5A5A5A5A5AULL};
    std::vector<tess::Coord3> free;
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x)
        if (grid[static_cast<std::size_t>(y) * N + x] != 0)
          free.push_back(tess::Coord3{x, y, 0});
    const auto s = free[rng.below(free.size())];
    const auto g = free[rng.below(free.size())];
    const tess::PathRequest req{s, g};
    const auto opt = oracle_cost(grid, N, s, g);

    tess::PathScratch a_scratch;
    const auto a = tess::astar_path<World, PassableTag>(
        *w, req, a_scratch, tess::MissingChunkPolicy::ReportIndeterminate);
    tess::PathScratch j_scratch;
    j_scratch.p4_bidir_ = true;
    const auto j = tess::astar_path<World, PassableTag>(
        *w, req, j_scratch, tess::MissingChunkPolicy::ReportIndeterminate);
    ++checked;

    // Reachability agreement with the oracle, both arms.
    CHECK((opt >= 0) == (a.status == tess::PathStatus::Found), "%s N=%d t=%u",
          family_name(family), N, trial);
    CHECK((opt >= 0) == (j.status == tess::PathStatus::Found), "%s N=%d t=%u",
          family_name(family), N, trial);
    if (opt < 0) continue;

    // Cost exactness, both arms, against the oracle.
    CHECK(a.cost == static_cast<std::uint32_t>(opt), "%s N=%d t=%u a=%u o=%d",
          family_name(family), N, trial, a.cost, opt);
    CHECK(j.cost == static_cast<std::uint32_t>(opt), "%s N=%d t=%u j=%u o=%d",
          family_name(family), N, trial, j.cost, opt);
    // Route validity, oracle-checked rather than incumbent-checked.
    CHECK(route_valid(*w, req, j.path, j.cost), "%s N=%d t=%u",
          family_name(family), N, trial);

    // Determinism: same input, same route, twice.
    tess::PathScratch j2_scratch;
    j2_scratch.p4_bidir_ = true;
    const auto j2 = tess::astar_path<World, PassableTag>(
        *w, req, j2_scratch, tess::MissingChunkPolicy::ReportIndeterminate);
    CHECK(std::equal(j.path.begin(), j.path.end(), j2.path.begin(),
                     j2.path.end()),
          "%s N=%d t=%u", family_name(family), N, trial);
    ++jps_served;
  }
}

// Ineligible callers: the flag must be ignored byte-identically. All
// five pre-registered ineligible instantiations run: hex, weighted,
// provider-composed, sparse, and 3D. Hex, weighted, and provider forms
// route to the weighted core before the JPS branch exists, so their
// checks are compile-and-equality trivial by design -- the point is
// that they INSTANTIATE with the flag set and answer identically.
template <typename World, typename RunPlain, typename RunFlagged>
void expect_flag_ignored(const char* label, RunPlain&& run_plain,
                         RunFlagged&& run_flagged) {
  const auto a = run_plain();
  const auto b = run_flagged();
  CHECK(a.status == b.status && a.cost == b.cost &&
            std::equal(a.path.begin(), a.path.end(), b.path.begin(),
                       b.path.end()),
        "%s gate", label);
}

void gate_checks() {
  {
    // 3D world (non-degenerate z): constexpr-excluded.
    using Shape3 =
        tess::Shape<tess::Extent3{16, 16, 4}, tess::Extent3{8, 8, 2}>;
    using World3 = tess::AlwaysResidentWorld<Shape3, Schema>;
    World3 w;
    for (auto& page : w.chunks()) {
      auto open = page.template field_span<PassableTag>();
      for (std::size_t i = 0; i < open.size(); ++i) open[i] = true;
    }
    const tess::PathRequest req{{0, 0, 0}, {7, 9, 3}};
    tess::PathScratch plain, flagged;
    flagged.p4_bidir_ = true;
    const auto a = tess::astar_path<World3, PassableTag>(
        w, req, plain, tess::MissingChunkPolicy::ReportIndeterminate);
    const auto b = tess::astar_path<World3, PassableTag>(
        w, req, flagged, tess::MissingChunkPolicy::ReportIndeterminate);
    CHECK(a.status == b.status && a.cost == b.cost &&
              std::equal(a.path.begin(), a.path.end(), b.path.begin(),
                         b.path.end()),
          "3d gate");
  }
  {
    // Sparse world: constexpr-excluded.
    using ShapeS =
        tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{32, 32, 1}>;
    using WorldS = tess::SparseResidentWorld<ShapeS, Schema>;
    WorldS w{tess::ResidencyConfig{2 * WorldS::page_byte_size}};
    for (auto key : {tess::ChunkKey{0}, tess::ChunkKey{1}}) {
      w.ensure_resident(key);
      auto& page = w.chunk(key);
      for (std::uint64_t i = 0; i < WorldS::local_tile_count; ++i) {
        page.template field<PassableTag>(tess::LocalTileId{i}) = true;
      }
    }
    const tess::PathRequest req{{0, 0, 0}, {60, 30, 0}};
    tess::PathScratch plain, flagged;
    flagged.p4_bidir_ = true;
    const auto a = tess::astar_path<WorldS, PassableTag>(
        w, req, plain, tess::MissingChunkPolicy::ReportIndeterminate);
    const auto b = tess::astar_path<WorldS, PassableTag>(
        w, req, flagged, tess::MissingChunkPolicy::ReportIndeterminate);
    CHECK(a.status == b.status && a.cost == b.cost &&
              std::equal(a.path.begin(), a.path.end(), b.path.begin(),
                         b.path.end()),
          "sparse gate");
  }
  {
    // Hex lattice: the unit entry point reroutes to the weighted core
    // before the JPS branch is even compiled for it.
    using HexShape = tess::Shape<tess::Extent3{8, 8, 1},
                                 tess::Extent3{4, 4, 1},
                                 tess::lattice::HexAxial>;
    using HexWorld = tess::AlwaysResidentWorld<HexShape, Schema>;
    HexWorld w;
    for (auto& page : w.chunks()) {
      auto open = page.template field_span<PassableTag>();
      for (std::size_t i = 0; i < open.size(); ++i) open[i] = true;
    }
    const tess::PathRequest req{{0, 0, 0}, {7, 7, 0}};
    tess::PathScratch plain, flagged;
    flagged.p4_bidir_ = true;
    const auto a = tess::astar_path<HexWorld, PassableTag>(
        w, req, plain, tess::MissingChunkPolicy::ReportIndeterminate);
    const auto b = tess::astar_path<HexWorld, PassableTag>(
        w, req, flagged, tess::MissingChunkPolicy::ReportIndeterminate);
    CHECK(a.status == b.status && a.cost == b.cost &&
              std::equal(a.path.begin(), a.path.end(), b.path.begin(),
                         b.path.end()),
          "hex gate");
  }
  {
    // Weighted class and provider-composed forms on an eligible-shaped
    // world: both go through weighted_astar_path, which has no JPS
    // branch at all; the flag must still be ignored byte-identically.
    using Shape = tess::Shape<tess::Extent3{32, 32, 1},
                              tess::Extent3{16, 16, 1}>;
    using CostSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                         tess::Field<CostTag, std::uint8_t>>;
    using World = tess::AlwaysResidentWorld<Shape, CostSchema>;
    using Weighted = tess::movement::MovementClass<
        tess::movement::Field<PassableTag>, tess::movement::FieldCost<CostTag>>;
    World w;
    for (auto& page : w.chunks()) {
      auto open = page.template field_span<PassableTag>();
      auto cost = page.template field_span<CostTag>();
      for (std::size_t i = 0; i < open.size(); ++i) {
        open[i] = true;
        cost[i] = static_cast<std::uint8_t>(1 + (i % 3));
      }
    }
    const tess::PathRequest req{{1, 1, 0}, {30, 27, 0}};
    {
      tess::PathScratch plain, flagged;
      flagged.p4_bidir_ = true;
      const auto a = tess::weighted_astar_path<World, Weighted>(
          w, req, plain, tess::MissingChunkPolicy::ReportIndeterminate);
      const auto b = tess::weighted_astar_path<World, Weighted>(
          w, req, flagged, tess::MissingChunkPolicy::ReportIndeterminate);
      CHECK(a.status == b.status && a.cost == b.cost &&
                std::equal(a.path.begin(), a.path.end(), b.path.begin(),
                           b.path.end()),
            "weighted gate");
    }
    {
      tess::PathScratch plain, flagged;
      flagged.p4_bidir_ = true;
      const auto provider = tess::AdjacentTransitions{};
      const auto a = tess::astar_path<World, PassableTag>(
          w, req, plain, tess::MissingChunkPolicy::ReportIndeterminate,
          provider);
      const auto b = tess::astar_path<World, PassableTag>(
          w, req, flagged, tess::MissingChunkPolicy::ReportIndeterminate,
          provider);
      CHECK(a.status == b.status && a.cost == b.cost &&
                std::equal(a.path.begin(), a.path.end(), b.path.begin(),
                           b.path.end()),
            "provider gate");
    }
  }
}

}  // namespace

int main() {
  int checked = 0, served = 0;
  for (const auto family : {FamilyKind::Open, FamilyKind::WallGap,
                            FamilyKind::Maze, FamilyKind::Rubble}) {
    run_family<64>(family, checked, served);
    run_family<256>(family, checked, served);
  }
  gate_checks();
  std::printf("\nchecked=%d found_and_verified=%d failures=%d\n", checked,
              served, failures);
  return failures == 0 ? 0 : 1;
}
```

## p4_counters.cc (wall-gap mechanism counters)

```cpp
// Counter comparison on the two decision-relevant rubble cells: what the
// JPS arm actually pays for. Build twice, with and without TESS_P4_BIDIR,
// both with TESS_ENABLE_DIAGNOSTICS.
#include <tess/tess.h>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
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
  auto below(std::uint64_t b) -> std::uint64_t { return ((next() >> 32) * b) >> 32; }
};
// Wall-gap terrain, matching the bench generator exactly: parallel
// vertical walls every N/8 columns, two-tile gaps at alternating ends.
template <int N>
void run(const char* label) {
  using Shape = tess::Shape<tess::Extent3{N, N, 1}, tess::Extent3{16, 16, 1}>;
  using World = tess::AlwaysResidentWorld<Shape, Schema>;
  auto w = std::make_unique<World>();
  const auto seed = 0x9E3779B97F4A7C15ULL * (1ULL * 1000003ULL + 1);
  std::vector<std::uint8_t> grid(static_cast<std::size_t>(N) * N, 1);
  {
    const int spacing = N / 8;
    int k = 0;
    for (int x = spacing; x < N - 1; x += spacing, ++k) {
      const bool gap_high = (k % 2) == 0;
      for (int y = 0; y < N; ++y) {
        const bool in_gap = gap_high ? (y >= N - 2) : (y <= 1);
        if (!in_gap) grid[static_cast<std::size_t>(y) * N + x] = 0;
      }
    }
  }
  for (auto& page : w->chunks()) {
    auto open = page.template field_span<PassableTag>();
    for (std::size_t i = 0; i < open.size(); ++i) open[i] = false;
  }
  std::vector<tess::Coord3> free;
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x)
      if (grid[static_cast<std::size_t>(y) * N + x] != 0) {
        w->template field<PassableTag>(tess::Coord3{x, y, 0}) = true;
        free.push_back(tess::Coord3{x, y, 0});
      }
  Rng qrng{seed ^ 0x5A5A5A5A5A5A5A5AULL};
  tess::diagnostics::PathCounters counters;
  tess::PathScratch scratch;
  scratch.reserve_nodes(static_cast<std::size_t>(N) * N);
#ifdef TESS_P4_BIDIR
  scratch.p4_bidir_ = true;
#endif
  {
    const tess::diagnostics::ScopedPathCounters scope{counters};
    for (unsigned trial = 0; trial < 20; ++trial) {
      const auto s = free[qrng.below(free.size())];
      const auto g = free[qrng.below(free.size())];
      const auto r = tess::astar_path<World, PassableTag>(
          *w, tess::PathRequest{s, g}, scratch,
          tess::MissingChunkPolicy::ReportIndeterminate);
      (void)r;
    }
  }
  std::printf(
      "%s N=%d: passability=%llu pushes=%llu pops=%llu stale=%llu "
      "closed=%llu touched=%llu heuristic=%llu reconstruct=%llu\n",
      label, N,
      static_cast<unsigned long long>(counters.passability_checks),
      static_cast<unsigned long long>(counters.heap_pushes),
      static_cast<unsigned long long>(counters.heap_pops),
      static_cast<unsigned long long>(counters.stale_pops),
      static_cast<unsigned long long>(counters.closed_pops),
      static_cast<unsigned long long>(counters.touched_nodes),
      static_cast<unsigned long long>(counters.heuristic_calls),
      static_cast<unsigned long long>(counters.reconstructed_nodes));
}
}  // namespace
int main() {
#ifdef TESS_P4_BIDIR
  const char* label = "bidir";
#else
  const char* label = "base";
#endif
  run<64>(label);
  run<256>(label);
}
```

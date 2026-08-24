# P5 measurement programs: recorded source

## p5_stage1.cc (corrected per issue #255 amendment 1)

The amendment-1 program: symmetric per-neighbor work accounting,
13-offset Manhattan-2 route-local sampling, an untimed pregeneration
pass deriving the edit/start trace from the incumbent's routes,
warm-path allocation and memory gates enforced, and the whole sweep
replayed twice with recorded digests. The superseded first version
(asymmetric counters; its capture is `stage1-v1-asymmetric.txt`) is
recoverable from this file's git history. Compile with
`-std=c++23 -O2 -DNDEBUG -DTESS_ENABLE_DIAGNOSTICS -Iinclude
-Ibuild/dev/generated/include`.

```cpp
// P5 stage 1 (issue #255, amendment 1): deterministic work-ratio
// feasibility for D* Lite incremental replanning against fresh
// canonical search, on the pre-registered churn trace. No library
// changes: the candidate is a program-local goal-keyed D* Lite over
// the same terrain the incumbent searches, oracle-checked on every
// answer. Amendment-1 conformance: symmetric per-neighbor work
// accounting, route-local offsets drawn from the 13-offset Manhattan-2
// table, an untimed pregeneration pass derives the edit/start trace
// from the INCUMBENT's routes (arm-independent), warm-path allocation
// gate enforced per arm, resident-structure bytes recorded, and the
// whole sweep replayed twice with recorded digests. Compile with
// -DTESS_ENABLE_DIAGNOSTICS (for the incumbent's counters); -O2 -DNDEBUG.
#include <tess/tess.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <new>
#include <vector>

// Program-local allocation accounting for the registered warm-path
// gate ("no warm-path allocation after the first cycle").
namespace {
std::uint64_t g_alloc_count = 0;
}
void* operator new(std::size_t size) {
  ++g_alloc_count;
  if (auto* p = std::malloc(size)) return p;
  std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

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

enum class FamilyKind { WallGap, Rubble };
const char* family_name(FamilyKind f) {
  return f == FamilyKind::WallGap ? "wall_gap" : "rubble";
}

std::vector<std::uint8_t> build_terrain(FamilyKind family, int n,
                                        std::uint64_t seed) {
  std::vector<std::uint8_t> grid(static_cast<std::size_t>(n) * n, 1);
  const auto close = [&](int x, int y) {
    grid[static_cast<std::size_t>(y) * n + x] = 0;
  };
  if (family == FamilyKind::WallGap) {
    const int spacing = n / 8;
    int k = 0;
    for (int x = spacing; x < n - 1; x += spacing, ++k) {
      const bool gap_high = (k % 2) == 0;
      for (int y = 0; y < n; ++y) {
        const bool in_gap = gap_high ? (y >= n - 2) : (y <= 1);
        if (!in_gap) close(x, y);
      }
    }
  } else {
    Rng rng{seed};
    for (int y = 0; y < n; ++y)
      for (int x = 0; x < n; ++x)
        if (rng.below(100) < 25) close(x, y);
  }
  return grid;
}

// Independent BFS oracle over the CURRENT grid.
int oracle_cost(const std::vector<std::uint8_t>& grid, int n, tess::Coord3 s,
                tess::Coord3 g) {
  if (grid[static_cast<std::size_t>(s.y) * n + s.x] == 0 ||
      grid[static_cast<std::size_t>(g.y) * n + g.x] == 0) {
    return -1;
  }
  std::vector<int> dist(static_cast<std::size_t>(n) * n, -1);
  std::deque<std::pair<int, int>> q{
      {static_cast<int>(s.x), static_cast<int>(s.y)}};
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

// ---------------------------------------------------------------------
// Program-local D* Lite, goal-keyed, unit cost, 4-connected, lazy
// deletion heap, km offset on start movement. Work counters mirror the
// primitive classes the incumbent's counters report: heap pushes, pops,
// and vertex relax/update attempts.
// ---------------------------------------------------------------------
class DStarLite {
 public:
  DStarLite(const std::vector<std::uint8_t>* grid, int n, tess::Coord3 goal)
      : grid_(grid),
        n_(n),
        goal_(static_cast<int>(goal.y) * n + static_cast<int>(goal.x)) {
    const auto cells = static_cast<std::size_t>(n) * n;
    g_.assign(cells, kInf);
    rhs_.assign(cells, kInf);
    rhs_[static_cast<std::size_t>(goal_)] = 0;
    push(goal_, key(goal_));
  }

  std::uint64_t pushes = 0;
  std::uint64_t pops = 0;
  std::uint64_t updates = 0;
  // Amendment 1: one count per neighbor examined in an rhs re-scan,
  // mirroring the incumbent's neighbor_candidates.
  std::uint64_t neighbor_scans = 0;
  std::size_t peak_heap = 0;

  [[nodiscard]] std::uint64_t work() const {
    return pushes + pops + updates + neighbor_scans;
  }
  void reserve(std::size_t heap_entries, std::size_t route_hint) {
    heap_.reserve(heap_entries);
    (void)route_hint;
  }
  [[nodiscard]] std::size_t resident_bytes() const {
    return g_.capacity() * sizeof(std::uint32_t) +
           rhs_.capacity() * sizeof(std::uint32_t) +
           heap_.capacity() * sizeof(std::pair<Key, int>);
  }

  void edit(int cell) {
    // Passability of `cell` flipped in the shared grid. Its own rhs and
    // every neighbor's rhs may change.
    update_vertex(cell);
    int nbs[4];
    const int count = neighbors(cell, nbs);
    for (int i = 0; i < count; ++i) {
      update_vertex(nbs[i]);
    }
  }

  // Returns oracle-comparable cost, or -1 for NoPath, filling route.
  int query(tess::Coord3 start_c, std::vector<tess::Coord3>& route) {
    const int start =
        static_cast<int>(start_c.y) * n_ + static_cast<int>(start_c.x);
    km_ += heuristic(last_start_, start);
    last_start_ = start;
    start_ = start;
    compute_shortest_path();
    route.clear();
    if (rhs_[static_cast<std::size_t>(start)] >= kInf) {
      return -1;
    }
    // Greedy descent over g: from start repeatedly step to the
    // neighbor minimizing g+1; ties by enumeration order.
    auto cur = start;
    route.push_back(coord(cur));
    std::size_t guard = 0;
    while (cur != goal_) {
      if (++guard > g_.size()) {
        return -2;  // descent failed; caught by the cost gate
      }
      int best = -1;
      std::uint32_t best_g = kInf;
      int nbs[4];
      const int count = neighbors(cur, nbs);
      for (int i = 0; i < count; ++i) {
        const auto nb = nbs[i];
        if (!open(nb)) continue;
        if (g_[static_cast<std::size_t>(nb)] < best_g) {
          best_g = g_[static_cast<std::size_t>(nb)];
          best = nb;
        }
      }
      if (best < 0 || best_g >= kInf) {
        return -2;
      }
      cur = best;
      route.push_back(coord(cur));
    }
    return static_cast<int>(route.size()) - 1;
  }

 private:
  static constexpr std::uint32_t kInf = 0x3FFFFFFF;
  using Key = std::pair<std::uint64_t, std::uint64_t>;

  const std::vector<std::uint8_t>* grid_;
  int n_;
  int goal_;
  int start_ = 0;
  int last_start_ = 0;
  std::uint64_t km_ = 0;
  std::vector<std::uint32_t> g_, rhs_;
  // Lazy-deletion binary heap of (key, cell).
  std::vector<std::pair<Key, int>> heap_;

  [[nodiscard]] bool open(int cell) const {
    return (*grid_)[static_cast<std::size_t>(cell)] != 0;
  }
  [[nodiscard]] tess::Coord3 coord(int cell) const {
    return tess::Coord3{cell % n_, cell / n_, 0};
  }
  [[nodiscard]] std::uint64_t heuristic(int a, int b) const {
    const auto ax = a % n_, ay = a / n_, bx = b % n_, by = b / n_;
    return static_cast<std::uint64_t>(std::abs(ax - bx) + std::abs(ay - by));
  }
  // Allocation-free neighbor iteration (warm-path allocation gate).
  int neighbors(int cell, int (&out)[4]) const {
    int count = 0;
    const auto x = cell % n_, y = cell / n_;
    if (x + 1 < n_) out[count++] = cell + 1;
    if (x > 0) out[count++] = cell - 1;
    if (y + 1 < n_) out[count++] = cell + n_;
    if (y > 0) out[count++] = cell - n_;
    return count;
  }
  [[nodiscard]] Key key(int cell) const {
    const auto m = std::min(g_[static_cast<std::size_t>(cell)],
                            rhs_[static_cast<std::size_t>(cell)]);
    return {static_cast<std::uint64_t>(m) + heuristic(start_, cell) + km_,
            m};
  }
  void push(int cell, Key k) {
    heap_.emplace_back(k, cell);
    peak_heap = std::max(peak_heap, heap_.size());
    std::push_heap(heap_.begin(), heap_.end(),
                   [](const auto& l, const auto& r) { return l > r; });
    ++pushes;
  }
  void update_vertex(int cell) {
    ++updates;
    if (cell != goal_) {
      std::uint32_t best = kInf;
      if (open(cell)) {
        int nbs[4];
        const int count = neighbors(cell, nbs);
        for (int i = 0; i < count; ++i) {
          ++neighbor_scans;
          if (!open(nbs[i])) continue;
          best = std::min(best, g_[static_cast<std::size_t>(nbs[i])] + 1);
        }
      }
      rhs_[static_cast<std::size_t>(cell)] = best;
    }
    if (g_[static_cast<std::size_t>(cell)] !=
        rhs_[static_cast<std::size_t>(cell)]) {
      push(cell, key(cell));
    }
  }
  void compute_shortest_path() {
    while (!heap_.empty()) {
      const auto top = heap_.front();
      const auto cell = top.second;
      const auto start_key = key(start_);
      const bool start_consistent =
          g_[static_cast<std::size_t>(start_)] ==
          rhs_[static_cast<std::size_t>(start_)];
      if (!(top.first < start_key) && start_consistent) {
        break;
      }
      std::pop_heap(heap_.begin(), heap_.end(),
                    [](const auto& l, const auto& r) { return l > r; });
      heap_.pop_back();
      ++pops;
      const auto fresh = key(cell);
      if (top.first < fresh) {
        push(cell, fresh);  // stale entry: re-key
        continue;
      }
      if (g_[static_cast<std::size_t>(cell)] ==
          rhs_[static_cast<std::size_t>(cell)]) {
        continue;  // already consistent: a duplicate entry, discard --
                   // processing it through the underconsistent branch
                   // toggles g to infinity and back forever
      }
      if (g_[static_cast<std::size_t>(cell)] >
          rhs_[static_cast<std::size_t>(cell)]) {
        g_[static_cast<std::size_t>(cell)] =
            rhs_[static_cast<std::size_t>(cell)];
        int nbs[4];
        const int count = neighbors(cell, nbs);
        for (int i = 0; i < count; ++i) {
          update_vertex(nbs[i]);
        }
      } else {
        g_[static_cast<std::size_t>(cell)] = kInf;
        update_vertex(cell);
        int nbs[4];
        const int count = neighbors(cell, nbs);
        for (int i = 0; i < count; ++i) {
          update_vertex(nbs[i]);
        }
      }
    }
  }
};

// ---------------------------------------------------------------------
// The trace runner.
// ---------------------------------------------------------------------

// The 13 offsets with |dx| + |dy| <= 2 (the registered route-local
// bound), drawn uniformly; bounded in-bounds redraws precede any
// uniform fallback, and fallbacks are counted and reported.
constexpr int kManhattan2[13][2] = {
    {0, 0},  {1, 0},  {-1, 0}, {0, 1},  {0, -1}, {2, 0},  {-2, 0},
    {0, 2},  {0, -2}, {1, 1},  {1, -1}, {-1, 1}, {-1, -1},
};

struct CellTrace {
  // Per cycle: the edit cells (already bounds-checked) and the start.
  std::vector<std::vector<int>> edits;
  std::vector<int> starts;  // -1 = cycle skipped (no free start found)
  std::uint64_t uniform_fallbacks = 0;
};

struct CellStats {
  double ratio = 0.0;
  std::uint64_t digest = 0xCBF29CE484222325ULL;
  std::uint64_t answers_checked = 0;
  std::uint64_t warm_allocs_dstar = 0;
  std::uint64_t warm_allocs_incumbent = 0;
  std::size_t dstar_bytes = 0;
  std::uint64_t fallbacks = 0;
};

// Untimed pregeneration: the edit/start trace derives from the
// INCUMBENT's routes only (amendment 1 clarification), so both arms --
// and both stage-2 binaries -- replay one identical workload.
template <int N, typename World>
CellTrace pregenerate(FamilyKind family, bool route_local,
                      int edits_per_cycle, std::uint64_t seed,
                      std::vector<std::uint8_t> grid, World& world,
                      const std::vector<int>& free_cells, int goal_cell,
                      tess::Coord3 goal) {
  CellTrace trace;
  Rng rng{seed ^ 0x5A5A5A5A5A5A5A5AULL};
  (void)rng.below(1);  // mirror the goal draw already consumed by caller
  tess::PathScratch scratch;
  scratch.reserve_nodes(static_cast<std::size_t>(N) * N);
  std::vector<tess::Coord3> last_route;
  for (int cycle = 0; cycle < 64; ++cycle) {
    auto& edits = trace.edits.emplace_back();
    for (int e = 0; e < edits_per_cycle; ++e) {
      int cell = -1;
      if (route_local && !last_route.empty()) {
        for (int attempt = 0; attempt < 8 && cell < 0; ++attempt) {
          const auto base = last_route[rng.below(last_route.size())];
          const auto& off = kManhattan2[rng.below(13)];
          const auto cx = base.x + off[0];
          const auto cy = base.y + off[1];
          if (cx >= 0 && cy >= 0 && cx < N && cy < N) {
            cell = static_cast<int>(cy) * N + static_cast<int>(cx);
          }
        }
        if (cell < 0) ++trace.uniform_fallbacks;
      }
      if (cell < 0) {
        cell = static_cast<int>(rng.below(static_cast<std::uint64_t>(N) * N));
      }
      if (cell == goal_cell) {
        edits.push_back(-1);  // recorded skip, keeps replay aligned
        continue;
      }
      edits.push_back(cell);
      grid[static_cast<std::size_t>(cell)] ^= 1;
      world.template field<PassableTag>(tess::Coord3{cell % N, cell / N, 0}) =
          grid[static_cast<std::size_t>(cell)] != 0;
    }
    int start_cell = free_cells[rng.below(free_cells.size())];
    std::size_t redraw_guard = 0;
    while (grid[static_cast<std::size_t>(start_cell)] == 0 &&
           redraw_guard++ < 1024) {
      start_cell = free_cells[rng.below(free_cells.size())];
    }
    if (grid[static_cast<std::size_t>(start_cell)] == 0) {
      trace.starts.push_back(-1);
      continue;
    }
    trace.starts.push_back(start_cell);
    const tess::Coord3 start{start_cell % N, start_cell / N, 0};
    const auto r = tess::astar_path<World, PassableTag>(
        world, tess::PathRequest{start, goal}, scratch,
        tess::MissingChunkPolicy::ReportIndeterminate);
    last_route.clear();
    if (r.status == tess::PathStatus::Found) {
      for (const auto step : r.path) last_route.push_back(step);
    }
  }
  return trace;
}

template <int N>
void run_cell(FamilyKind family, bool route_local, int edits_per_cycle,
              std::vector<CellStats>& stats_out) {
  using Shape = tess::Shape<tess::Extent3{N, N, 1}, tess::Extent3{16, 16, 1}>;
  using World = tess::AlwaysResidentWorld<Shape, Schema>;
  for (unsigned trial = 0; trial < 10; ++trial) {
    const auto seed =
        0x9E3779B97F4A7C15ULL *
        (static_cast<std::uint64_t>(family) * 1000003ULL + trial + 1);
    auto grid = build_terrain(family, N, seed);
    auto world = std::make_unique<World>();
    const auto sync_world = [&] {
      for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
          world->template field<PassableTag>(tess::Coord3{x, y, 0}) =
              grid[static_cast<std::size_t>(y) * N + x] != 0;
    };
    sync_world();
    std::vector<int> free_cells;
    for (int c = 0; c < N * N; ++c)
      if (grid[static_cast<std::size_t>(c)] != 0) free_cells.push_back(c);
    Rng goal_rng{seed ^ 0x5A5A5A5A5A5A5A5AULL};
    const auto goal_cell = free_cells[goal_rng.below(free_cells.size())];
    const tess::Coord3 goal{goal_cell % N, goal_cell / N, 0};

    const auto trace = pregenerate<N>(family, route_local, edits_per_cycle,
                                      seed, grid, *world, free_cells,
                                      goal_cell, goal);
    sync_world();  // pregeneration mutated the world; restore

    CellStats stats;
    stats.fallbacks = trace.uniform_fallbacks;
    const auto mix = [&stats](std::uint64_t value) {
      stats.digest ^= value + 0x9E3779B97F4A7C15ULL + (stats.digest << 6U) +
                      (stats.digest >> 2U);
    };
    DStarLite dstar(&grid, N, goal);
    dstar.reserve(static_cast<std::size_t>(N) * N * 8,
                  static_cast<std::size_t>(N) * N);
    tess::PathScratch scratch;
    scratch.reserve_nodes(static_cast<std::size_t>(N) * N);
    std::vector<tess::Coord3> route;
    route.reserve(static_cast<std::size_t>(N) * N);
    std::uint64_t incumbent_work = 0;
    for (int cycle = 0; cycle < 64; ++cycle) {
      for (const auto cell : trace.edits[static_cast<std::size_t>(cycle)]) {
        if (cell < 0) continue;
        grid[static_cast<std::size_t>(cell)] ^= 1;
        world->template field<PassableTag>(
            tess::Coord3{cell % N, cell / N, 0}) =
            grid[static_cast<std::size_t>(cell)] != 0;
        dstar.edit(cell);
      }
      const auto start_cell = trace.starts[static_cast<std::size_t>(cycle)];
      if (start_cell < 0) continue;
      const tess::Coord3 start{start_cell % N, start_cell / N, 0};

      const auto d_alloc_before = g_alloc_count;
      const auto before = dstar.work();
      const auto d_cost = dstar.query(start, route);
      const auto d_work = dstar.work() - before;
      const auto d_allocs = g_alloc_count - d_alloc_before;

      tess::diagnostics::PathCounters counters;
      int a_cost = -1;
      const auto a_alloc_before = g_alloc_count;
      {
        const tess::diagnostics::ScopedPathCounters scope{counters};
        const auto r = tess::astar_path<World, PassableTag>(
            *world, tess::PathRequest{start, goal}, scratch,
            tess::MissingChunkPolicy::ReportIndeterminate);
        a_cost = r.status == tess::PathStatus::Found
                     ? static_cast<int>(r.cost)
                     : -1;
      }
      const auto a_allocs = g_alloc_count - a_alloc_before;
      const auto a_work = counters.heap_pushes + counters.heap_pops +
                          counters.relax_attempts +
                          counters.neighbor_candidates;
      incumbent_work += a_work;
      if (cycle >= 1) {
        // Registered warm-path allocation gate, per arm.
        stats.warm_allocs_dstar += d_allocs;
        stats.warm_allocs_incumbent += a_allocs;
      }

      // Correctness + invalidation gates on EVERY answer: the oracle
      // runs on the post-edit grid, so a stale or omitted invalidation
      // in either arm surfaces as a cost mismatch here.
      const auto opt = oracle_cost(grid, N, start, goal);
      ++stats.answers_checked;
      CHECK(a_cost == opt, "%s N=%d t=%u c=%d incumbent=%d oracle=%d",
            family_name(family), N, trial, cycle, a_cost, opt);
      const auto d_reported = d_cost < 0 ? -1 : d_cost;
      CHECK(d_reported == opt, "%s N=%d t=%u c=%d dstar=%d oracle=%d",
            family_name(family), N, trial, cycle, d_reported, opt);
      if (opt >= 0) {
        bool valid = route.size() == static_cast<std::size_t>(opt) + 1 &&
                     route.front() == start && route.back() == goal;
        for (std::size_t i = 1; i < route.size() && valid; ++i) {
          const auto ddx = route[i].x - route[i - 1].x;
          const auto ddy = route[i].y - route[i - 1].y;
          valid = (ddx * ddx + ddy * ddy) == 1 &&
                  grid[static_cast<std::size_t>(route[i].y) * N +
                       route[i].x] != 0;
        }
        CHECK(valid, "%s N=%d t=%u c=%d route", family_name(family), N,
              trial, cycle);
      }
      mix(static_cast<std::uint64_t>(d_cost + 2));
      mix(static_cast<std::uint64_t>(a_cost + 2));
      mix(d_work);
      mix(a_work);
    }
    const auto d_total = dstar.work();
    stats.dstar_bytes = dstar.resident_bytes();
    if (d_total > 0) {
      stats.ratio = static_cast<double>(incumbent_work) /
                    static_cast<double>(d_total);
      stats_out.push_back(stats);
    }
  }
}

}  // namespace

std::uint64_t sweep(bool print_cells) {
  std::uint64_t digest = 0xCBF29CE484222325ULL;
  const auto mix = [&digest](std::uint64_t value) {
    digest ^= value + 0x9E3779B97F4A7C15ULL + (digest << 6U) +
              (digest >> 2U);
  };
  std::vector<double> all_ratios;
  std::uint64_t warm_dstar = 0, warm_incumbent = 0, fallbacks = 0,
                answers = 0;
  std::size_t peak_bytes = 0;
  for (const auto family : {FamilyKind::WallGap, FamilyKind::Rubble}) {
    for (const bool route_local : {true, false}) {
      for (const int edits : {1, 4, 16}) {
        std::vector<CellStats> s64, s256;
        run_cell<64>(family, route_local, edits, s64);
        run_cell<256>(family, route_local, edits, s256);
        const auto med = [](std::vector<CellStats> v) {
          std::sort(v.begin(), v.end(),
                    [](const CellStats& l, const CellStats& r) {
                      return l.ratio < r.ratio;
                    });
          return v.empty() ? 0.0 : v[v.size() / 2].ratio;
        };
        if (print_cells) {
          std::printf(
              "p5 %-8s local=%d E=%-2d median symmetric ratio: 64=%0.3f "
              "256=%0.3f\n",
              family_name(family), route_local ? 1 : 0, edits, med(s64),
              med(s256));
        }
        for (const auto* bucket : {&s64, &s256}) {
          for (const auto& cs : *bucket) {
            all_ratios.push_back(cs.ratio);
            warm_dstar += cs.warm_allocs_dstar;
            warm_incumbent += cs.warm_allocs_incumbent;
            fallbacks += cs.fallbacks;
            answers += cs.answers_checked;
            peak_bytes = std::max(peak_bytes, cs.dstar_bytes);
            mix(cs.digest);
          }
        }
      }
    }
  }
  std::sort(all_ratios.begin(), all_ratios.end());
  const auto pooled =
      all_ratios.empty() ? 0.0 : all_ratios[all_ratios.size() / 2];
  if (print_cells) {
    std::printf(
        "\ninvalidation gate (oracle equality after every edit batch): "
        "%s over %llu answers\n",
        failures == 0 ? "PASS" : "FAIL",
        static_cast<unsigned long long>(answers));
    std::printf(
        "warm-path allocation gate (cycles >= 1): dstar=%llu "
        "incumbent=%llu (bar: 0 each)\n",
        static_cast<unsigned long long>(warm_dstar),
        static_cast<unsigned long long>(warm_incumbent));
    CHECK(warm_dstar == 0, "dstar warm allocations");
    CHECK(warm_incumbent == 0, "incumbent warm allocations");
    std::printf(
        "memory: peak D* Lite resident state %zu bytes (g+rhs+heap, "
        "largest cell)\n",
        peak_bytes);
    std::printf(
        "route-local uniform fallbacks after bounded redraws: %llu\n",
        static_cast<unsigned long long>(fallbacks));
    std::printf(
        "\npooled median incumbent/candidate SYMMETRIC work ratio = "
        "%0.3f (stage-1 bar: >= 1.5)\n",
        pooled);
    std::printf("stage 1 -> %s\n", pooled >= 1.5
                                        ? "PROCEED TO TIMING"
                                        : "REJECT WITHOUT HARDWARE");
  }
  mix(static_cast<std::uint64_t>(pooled * 1e6));
  return digest;
}

int main() {
  const auto digest_run1 = sweep(true);
  const auto digest_run2 = sweep(false);
  std::printf("replay digests: run1=%016llx run2=%016llx -> %s\n",
              static_cast<unsigned long long>(digest_run1),
              static_cast<unsigned long long>(digest_run2),
              digest_run1 == digest_run2 ? "IDENTICAL" : "MISMATCH");
  CHECK(digest_run1 == digest_run2, "whole-sweep replay determinism");
  std::printf("correctness+conformance gates: %s (%d)\n",
              failures == 0 ? "ALL PASSED" : "FAILURES", failures);
  return failures == 0 ? 0 : 1;
}
```

The stage-2 bench below is the capture that produced the retained M3
JSONs; it predates amendment 1, and its route-local cells time
arm-dependent edit streams (the caveat recorded in README.md). It is
retained verbatim as context; stage 2 was not rerun because the
corrected stage 1 rejects under the registration's stop condition.

## bench/tess_p5_dstar_bench.cc + CMake wiring (stage 2, branch-only)

```diff
diff --git a/bench/CMakeLists.txt b/bench/CMakeLists.txt
index 14022522..05c63293 100644
--- a/bench/CMakeLists.txt
+++ b/bench/CMakeLists.txt
@@ -29,6 +29,13 @@ add_executable(
 target_link_libraries(tess_bench PRIVATE tess::tess benchmark::benchmark_main)
 tess_apply_project_options(tess_bench)

+# P5 screen target, branch-only, never merged: one source, two arms via
+# -DTESS_P5_DSTAR on the compile line of a separate build tree.
+add_executable(tess_p5_dstar_bench tess_p5_dstar_bench.cc)
+target_link_libraries(tess_p5_dstar_bench PRIVATE tess::tess
+                                                  benchmark::benchmark)
+tess_apply_project_options(tess_p5_dstar_bench)
+
 add_executable(
   tess_bench_diagnostics
   tess_bench.cc
diff --git a/bench/tess_p5_dstar_bench.cc b/bench/tess_p5_dstar_bench.cc
new file mode 100644
index 00000000..bd6ef15e
--- /dev/null
+++ b/bench/tess_p5_dstar_bench.cc
@@ -0,0 +1,353 @@
+// P5 stage 2 (issue #255): wall-time on the same pre-registered churn
+// trace stage 1 ran. One source builds both arms: without TESS_P5_DSTAR
+// each cycle answers with fresh canonical search; with it, the
+// program-local D* Lite (stage-1-verified oracle-exact) repairs and
+// answers. Per iteration: one full 64-cycle trial (trial 0 of the
+// cell), including candidate construction, so per-goal setup is inside
+// the measurement rather than hidden.
+#include <benchmark/benchmark.h>
+#include <tess/tess.h>
+
+#include <algorithm>
+#include <cstdio>
+#include <deque>
+#include <memory>
+#include <vector>
+
+namespace {
+
+struct PassableTag {};
+using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;
+
+struct Rng {
+  std::uint64_t s;
+  auto next() -> std::uint64_t {
+    s += 0x9E3779B97F4A7C15ULL;
+    std::uint64_t z = s;
+    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
+    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
+    return z ^ (z >> 31);
+  }
+  auto below(std::uint64_t b) -> std::uint64_t {
+    return ((next() >> 32) * b) >> 32;
+  }
+};
+
+enum class FamilyKind { WallGap, Rubble };
+const char* family_name(FamilyKind f) {
+  return f == FamilyKind::WallGap ? "wall_gap" : "rubble";
+}
+
+std::vector<std::uint8_t> build_terrain(FamilyKind family, int n,
+                                        std::uint64_t seed) {
+  std::vector<std::uint8_t> grid(static_cast<std::size_t>(n) * n, 1);
+  const auto close = [&](int x, int y) {
+    grid[static_cast<std::size_t>(y) * n + x] = 0;
+  };
+  if (family == FamilyKind::WallGap) {
+    const int spacing = n / 8;
+    int k = 0;
+    for (int x = spacing; x < n - 1; x += spacing, ++k) {
+      const bool gap_high = (k % 2) == 0;
+      for (int y = 0; y < n; ++y) {
+        const bool in_gap = gap_high ? (y >= n - 2) : (y <= 1);
+        if (!in_gap) close(x, y);
+      }
+    }
+  } else {
+    Rng rng{seed};
+    for (int y = 0; y < n; ++y)
+      for (int x = 0; x < n; ++x)
+        if (rng.below(100) < 25) close(x, y);
+  }
+  return grid;
+}
+
+// ---------------------------------------------------------------------
+// Program-local D* Lite, goal-keyed, unit cost, 4-connected, lazy
+// deletion heap, km offset on start movement. Work counters mirror the
+// primitive classes the incumbent's counters report: heap pushes, pops,
+// and vertex relax/update attempts.
+// ---------------------------------------------------------------------
+class DStarLite {
+ public:
+  DStarLite(const std::vector<std::uint8_t>* grid, int n, tess::Coord3 goal)
+      : grid_(grid),
+        n_(n),
+        goal_(static_cast<int>(goal.y) * n + static_cast<int>(goal.x)) {
+    const auto cells = static_cast<std::size_t>(n) * n;
+    g_.assign(cells, kInf);
+    rhs_.assign(cells, kInf);
+    rhs_[static_cast<std::size_t>(goal_)] = 0;
+    push(goal_, key(goal_));
+  }
+
+  std::uint64_t pushes = 0;
+  std::uint64_t pops = 0;
+  std::uint64_t updates = 0;
+
+  [[nodiscard]] std::uint64_t work() const { return pushes + pops + updates; }
+
+  void edit(int cell) {
+    // Passability of `cell` flipped in the shared grid. Its own rhs and
+    // every neighbor's rhs may change.
+    update_vertex(cell);
+    for (const auto nb : neighbors(cell)) {
+      update_vertex(nb);
+    }
+  }
+
+  // Returns oracle-comparable cost, or -1 for NoPath, filling route.
+  int query(tess::Coord3 start_c, std::vector<tess::Coord3>& route) {
+    const int start =
+        static_cast<int>(start_c.y) * n_ + static_cast<int>(start_c.x);
+    km_ += heuristic(last_start_, start);
+    last_start_ = start;
+    start_ = start;
+    compute_shortest_path();
+    route.clear();
+    if (rhs_[static_cast<std::size_t>(start)] >= kInf) {
+      return -1;
+    }
+    // Greedy descent over g: from start repeatedly step to the
+    // neighbor minimizing g+1; ties by enumeration order.
+    auto cur = start;
+    route.push_back(coord(cur));
+    std::size_t guard = 0;
+    while (cur != goal_) {
+      if (++guard > g_.size()) {
+        return -2;  // descent failed; caught by the cost gate
+      }
+      int best = -1;
+      std::uint32_t best_g = kInf;
+      for (const auto nb : neighbors(cur)) {
+        if (!open(nb)) continue;
+        if (g_[static_cast<std::size_t>(nb)] < best_g) {
+          best_g = g_[static_cast<std::size_t>(nb)];
+          best = nb;
+        }
+      }
+      if (best < 0 || best_g >= kInf) {
+        return -2;
+      }
+      cur = best;
+      route.push_back(coord(cur));
+    }
+    return static_cast<int>(route.size()) - 1;
+  }
+
+ private:
+  static constexpr std::uint32_t kInf = 0x3FFFFFFF;
+  using Key = std::pair<std::uint64_t, std::uint64_t>;
+
+  const std::vector<std::uint8_t>* grid_;
+  int n_;
+  int goal_;
+  int start_ = 0;
+  int last_start_ = 0;
+  std::uint64_t km_ = 0;
+  std::vector<std::uint32_t> g_, rhs_;
+  // Lazy-deletion binary heap of (key, cell).
+  std::vector<std::pair<Key, int>> heap_;
+
+  [[nodiscard]] bool open(int cell) const {
+    return (*grid_)[static_cast<std::size_t>(cell)] != 0;
+  }
+  [[nodiscard]] tess::Coord3 coord(int cell) const {
+    return tess::Coord3{cell % n_, cell / n_, 0};
+  }
+  [[nodiscard]] std::uint64_t heuristic(int a, int b) const {
+    const auto ax = a % n_, ay = a / n_, bx = b % n_, by = b / n_;
+    return static_cast<std::uint64_t>(std::abs(ax - bx) + std::abs(ay - by));
+  }
+  [[nodiscard]] std::vector<int> neighbors(int cell) const {
+    std::vector<int> out;
+    const auto x = cell % n_, y = cell / n_;
+    if (x + 1 < n_) out.push_back(cell + 1);
+    if (x > 0) out.push_back(cell - 1);
+    if (y + 1 < n_) out.push_back(cell + n_);
+    if (y > 0) out.push_back(cell - n_);
+    return out;
+  }
+  [[nodiscard]] Key key(int cell) const {
+    const auto m = std::min(g_[static_cast<std::size_t>(cell)],
+                            rhs_[static_cast<std::size_t>(cell)]);
+    return {static_cast<std::uint64_t>(m) + heuristic(start_, cell) + km_,
+            m};
+  }
+  void push(int cell, Key k) {
+    heap_.emplace_back(k, cell);
+    std::push_heap(heap_.begin(), heap_.end(),
+                   [](const auto& l, const auto& r) { return l > r; });
+    ++pushes;
+  }
+  void update_vertex(int cell) {
+    ++updates;
+    if (cell != goal_) {
+      std::uint32_t best = kInf;
+      if (open(cell)) {
+        for (const auto nb : neighbors(cell)) {
+          if (!open(nb)) continue;
+          best = std::min(best, g_[static_cast<std::size_t>(nb)] + 1);
+        }
+      }
+      rhs_[static_cast<std::size_t>(cell)] = best;
+    }
+    if (g_[static_cast<std::size_t>(cell)] !=
+        rhs_[static_cast<std::size_t>(cell)]) {
+      push(cell, key(cell));
+    }
+  }
+  void compute_shortest_path() {
+    while (!heap_.empty()) {
+      const auto top = heap_.front();
+      const auto cell = top.second;
+      const auto start_key = key(start_);
+      const bool start_consistent =
+          g_[static_cast<std::size_t>(start_)] ==
+          rhs_[static_cast<std::size_t>(start_)];
+      if (!(top.first < start_key) && start_consistent) {
+        break;
+      }
+      std::pop_heap(heap_.begin(), heap_.end(),
+                    [](const auto& l, const auto& r) { return l > r; });
+      heap_.pop_back();
+      ++pops;
+      const auto fresh = key(cell);
+      if (top.first < fresh) {
+        push(cell, fresh);  // stale entry: re-key
+        continue;
+      }
+      if (g_[static_cast<std::size_t>(cell)] ==
+          rhs_[static_cast<std::size_t>(cell)]) {
+        continue;  // already consistent: a duplicate entry, discard --
+                   // processing it through the underconsistent branch
+                   // toggles g to infinity and back forever
+      }
+      if (g_[static_cast<std::size_t>(cell)] >
+          rhs_[static_cast<std::size_t>(cell)]) {
+        g_[static_cast<std::size_t>(cell)] =
+            rhs_[static_cast<std::size_t>(cell)];
+        for (const auto nb : neighbors(cell)) {
+          update_vertex(nb);
+        }
+      } else {
+        g_[static_cast<std::size_t>(cell)] = kInf;
+        update_vertex(cell);
+        for (const auto nb : neighbors(cell)) {
+          update_vertex(nb);
+        }
+      }
+    }
+  }
+};
+
+
+template <int N, FamilyKind F, bool Local, int Edits>
+void run_trace(benchmark::State& state) {
+  using Shape = tess::Shape<tess::Extent3{N, N, 1}, tess::Extent3{16, 16, 1}>;
+  using World = tess::AlwaysResidentWorld<Shape, Schema>;
+  const auto seed = 0x9E3779B97F4A7C15ULL *
+                    (static_cast<std::uint64_t>(F) * 1000003ULL + 1);
+  const auto base_grid = build_terrain(F, N, seed);
+  auto world = std::make_unique<World>();
+  std::vector<int> base_free;
+  for (int c = 0; c < N * N; ++c)
+    if (base_grid[static_cast<std::size_t>(c)] != 0) base_free.push_back(c);
+  tess::PathScratch scratch;
+  scratch.reserve_nodes(static_cast<std::size_t>(N) * N);
+  for (auto _ : state) {
+    auto grid = base_grid;
+    for (int y = 0; y < N; ++y)
+      for (int x = 0; x < N; ++x)
+        world->template field<PassableTag>(tess::Coord3{x, y, 0}) =
+            grid[static_cast<std::size_t>(y) * N + x] != 0;
+    Rng rng{seed ^ 0x5A5A5A5A5A5A5A5AULL};
+    const auto goal_cell = base_free[rng.below(base_free.size())];
+    const tess::Coord3 goal{goal_cell % N, goal_cell / N, 0};
+#ifdef TESS_P5_DSTAR
+    DStarLite dstar(&grid, N, goal);
+    std::vector<tess::Coord3> route;
+#endif
+    std::vector<tess::Coord3> last_route;
+    std::uint64_t sink = 0;
+    for (int cycle = 0; cycle < 64; ++cycle) {
+      for (int e = 0; e < Edits; ++e) {
+        int cell = -1;
+        if (Local && !last_route.empty()) {
+          const auto base = last_route[rng.below(last_route.size())];
+          const auto dx = static_cast<std::int64_t>(rng.below(5)) - 2;
+          const auto dy = static_cast<std::int64_t>(rng.below(5)) - 2;
+          const auto cx = base.x + dx;
+          const auto cy = base.y + dy;
+          if (cx >= 0 && cy >= 0 && cx < N && cy < N) {
+            cell = static_cast<int>(cy) * N + static_cast<int>(cx);
+          }
+        }
+        if (cell < 0) {
+          cell =
+              static_cast<int>(rng.below(static_cast<std::uint64_t>(N) * N));
+        }
+        if (cell == goal_cell) continue;
+        grid[static_cast<std::size_t>(cell)] ^= 1;
+        world->template field<PassableTag>(
+            tess::Coord3{cell % N, cell / N, 0}) =
+            grid[static_cast<std::size_t>(cell)] != 0;
+#ifdef TESS_P5_DSTAR
+        dstar.edit(cell);
+#endif
+      }
+      int start_cell = base_free[rng.below(base_free.size())];
+      std::size_t redraw_guard = 0;
+      while (grid[static_cast<std::size_t>(start_cell)] == 0 &&
+             redraw_guard++ < 1024) {
+        start_cell = base_free[rng.below(base_free.size())];
+      }
+      if (grid[static_cast<std::size_t>(start_cell)] == 0) continue;
+      const tess::Coord3 start{start_cell % N, start_cell / N, 0};
+#ifdef TESS_P5_DSTAR
+      const auto c = dstar.query(start, route);
+      sink += static_cast<std::uint64_t>(c + 2);
+      if (c >= 0) last_route = route;
+#else
+      const auto r = tess::astar_path<World, PassableTag>(
+          *world, tess::PathRequest{start, goal}, scratch,
+          tess::MissingChunkPolicy::ReportIndeterminate);
+      sink += r.cost + static_cast<std::uint64_t>(r.status);
+      if (r.status == tess::PathStatus::Found) {
+        last_route.assign(r.path.begin(), r.path.end());
+      }
+#endif
+    }
+    benchmark::DoNotOptimize(sink);
+  }
+}
+
+#define P5_CELL(fam, famtag, local, edits)                                  \
+  void BM_p5_##fam##_##local##_##edits##_64(benchmark::State& s) {          \
+    run_trace<64, famtag, local == 1, edits>(s);                            \
+  }                                                                         \
+  void BM_p5_##fam##_##local##_##edits##_256(benchmark::State& s) {         \
+    run_trace<256, famtag, local == 1, edits>(s);                           \
+  }                                                                         \
+  BENCHMARK(BM_p5_##fam##_##local##_##edits##_64)                           \
+      ->Name("p5/trace_" #fam "_L" #local "_E" #edits "_64");               \
+  BENCHMARK(BM_p5_##fam##_##local##_##edits##_256)                          \
+      ->Name("p5/trace_" #fam "_L" #local "_E" #edits "_256");
+
+P5_CELL(wall_gap, FamilyKind::WallGap, 1, 1)
+P5_CELL(wall_gap, FamilyKind::WallGap, 1, 4)
+P5_CELL(wall_gap, FamilyKind::WallGap, 1, 16)
+P5_CELL(wall_gap, FamilyKind::WallGap, 0, 1)
+P5_CELL(wall_gap, FamilyKind::WallGap, 0, 4)
+P5_CELL(wall_gap, FamilyKind::WallGap, 0, 16)
+P5_CELL(rubble, FamilyKind::Rubble, 1, 1)
+P5_CELL(rubble, FamilyKind::Rubble, 1, 4)
+P5_CELL(rubble, FamilyKind::Rubble, 1, 16)
+P5_CELL(rubble, FamilyKind::Rubble, 0, 1)
+P5_CELL(rubble, FamilyKind::Rubble, 0, 4)
+P5_CELL(rubble, FamilyKind::Rubble, 0, 16)
+
+}  // namespace
+
+BENCHMARK_MAIN();

```

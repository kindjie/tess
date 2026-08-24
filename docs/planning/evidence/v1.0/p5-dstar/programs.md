# P5 measurement programs: recorded source

No library change exists anywhere in this screen: the candidate is a
program-local D* Lite, and the incumbent is called through its public
entry point. The stage-2 bench is additionally recorded as a tracked
diff below (intent-to-add, per the P3 lesson).

## p5_stage1.cc (work-ratio feasibility + all correctness gates)

```cpp
// P5 stage 1 (issue #255): deterministic work-ratio feasibility for
// D* Lite incremental replanning against fresh canonical search, on the
// pre-registered churn trace. No library changes: the candidate is a
// program-local goal-keyed D* Lite over the same terrain the incumbent
// searches, oracle-checked on every answer. Compile with
// -DTESS_ENABLE_DIAGNOSTICS (for the incumbent's counters); -O2 -DNDEBUG.
#include <tess/tess.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <memory>
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

  [[nodiscard]] std::uint64_t work() const { return pushes + pops + updates; }

  void edit(int cell) {
    // Passability of `cell` flipped in the shared grid. Its own rhs and
    // every neighbor's rhs may change.
    update_vertex(cell);
    for (const auto nb : neighbors(cell)) {
      update_vertex(nb);
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
      for (const auto nb : neighbors(cur)) {
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
  [[nodiscard]] std::vector<int> neighbors(int cell) const {
    std::vector<int> out;
    const auto x = cell % n_, y = cell / n_;
    if (x + 1 < n_) out.push_back(cell + 1);
    if (x > 0) out.push_back(cell - 1);
    if (y + 1 < n_) out.push_back(cell + n_);
    if (y > 0) out.push_back(cell - n_);
    return out;
  }
  [[nodiscard]] Key key(int cell) const {
    const auto m = std::min(g_[static_cast<std::size_t>(cell)],
                            rhs_[static_cast<std::size_t>(cell)]);
    return {static_cast<std::uint64_t>(m) + heuristic(start_, cell) + km_,
            m};
  }
  void push(int cell, Key k) {
    heap_.emplace_back(k, cell);
    std::push_heap(heap_.begin(), heap_.end(),
                   [](const auto& l, const auto& r) { return l > r; });
    ++pushes;
  }
  void update_vertex(int cell) {
    ++updates;
    if (cell != goal_) {
      std::uint32_t best = kInf;
      if (open(cell)) {
        for (const auto nb : neighbors(cell)) {
          if (!open(nb)) continue;
          best = std::min(best, g_[static_cast<std::size_t>(nb)] + 1);
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
        for (const auto nb : neighbors(cell)) {
          update_vertex(nb);
        }
      } else {
        g_[static_cast<std::size_t>(cell)] = kInf;
        update_vertex(cell);
        for (const auto nb : neighbors(cell)) {
          update_vertex(nb);
        }
      }
    }
  }
};

// ---------------------------------------------------------------------
// The trace runner.
// ---------------------------------------------------------------------

template <int N>
void run_cell(FamilyKind family, bool route_local, int edits_per_cycle,
              std::vector<double>& ratios) {
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
    Rng rng{seed ^ 0x5A5A5A5A5A5A5A5AULL};
    const auto goal_cell = free_cells[rng.below(free_cells.size())];
    const tess::Coord3 goal{goal_cell % N, goal_cell / N, 0};

    DStarLite dstar(&grid, N, goal);
    tess::PathScratch scratch;
    scratch.reserve_nodes(static_cast<std::size_t>(N) * N);
    std::vector<tess::Coord3> route;
    std::vector<tess::Coord3> last_route;
    std::uint64_t incumbent_work = 0;
    for (int cycle = 0; cycle < 64; ++cycle) {
      // E edits: toggle passability; never the goal tile; route-local
      // draws a tile within Manhattan 2 of the previous route when one
      // exists.
      for (int e = 0; e < edits_per_cycle; ++e) {
        int cell = -1;
        if (route_local && !last_route.empty()) {
          const auto base =
              last_route[rng.below(last_route.size())];
          const auto dx = static_cast<std::int64_t>(rng.below(5)) - 2;
          const auto dy = static_cast<std::int64_t>(rng.below(5)) - 2;
          const auto cx = base.x + dx;
          const auto cy = base.y + dy;
          if (cx >= 0 && cy >= 0 && cx < N && cy < N) {
            cell = static_cast<int>(cy) * N + static_cast<int>(cx);
          }
        }
        if (cell < 0) {
          cell = static_cast<int>(rng.below(static_cast<std::uint64_t>(N) * N));
        }
        if (cell == goal_cell) continue;
        grid[static_cast<std::size_t>(cell)] ^= 1;
        world->template field<PassableTag>(
            tess::Coord3{cell % N, cell / N, 0}) =
            grid[static_cast<std::size_t>(cell)] != 0;
        dstar.edit(cell);
      }
      // Query from a start that is free NOW (deterministic redraw).
      int start_cell = free_cells[rng.below(free_cells.size())];
      std::size_t redraw_guard = 0;
      while (grid[static_cast<std::size_t>(start_cell)] == 0 &&
             redraw_guard++ < 1024) {
        start_cell = free_cells[rng.below(free_cells.size())];
      }
      if (grid[static_cast<std::size_t>(start_cell)] == 0) continue;
      const tess::Coord3 start{start_cell % N, start_cell / N, 0};

      const auto before = dstar.work();
      const auto d_cost = dstar.query(start, route);
      const auto d_work = dstar.work() - before;

      tess::diagnostics::PathCounters counters;
      int a_cost = -1;
      {
        const tess::diagnostics::ScopedPathCounters scope{counters};
        const auto r = tess::astar_path<World, PassableTag>(
            *world, tess::PathRequest{start, goal}, scratch,
            tess::MissingChunkPolicy::ReportIndeterminate);
        a_cost = r.status == tess::PathStatus::Found
                     ? static_cast<int>(r.cost)
                     : -1;
      }
      const auto a_work = counters.heap_pushes + counters.heap_pops +
                          counters.relax_attempts +
                          counters.neighbor_candidates;
      incumbent_work += a_work;

      // Correctness gates on EVERY answer.
      const auto opt = oracle_cost(grid, N, start, goal);
      CHECK(a_cost == opt, "%s N=%d t=%u c=%d incumbent=%d oracle=%d",
            family_name(family), N, trial, cycle, a_cost, opt);
      const auto d_reported = d_cost < 0 ? -1 : d_cost;
      CHECK(d_reported == opt, "%s N=%d t=%u c=%d dstar=%d oracle=%d",
            family_name(family), N, trial, cycle, d_reported, opt);
      if (opt >= 0) {
        // Route validity for the D* Lite answer.
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
        last_route = route;
      }
      // Per-cycle ratio accumulates at trial level below.
      (void)d_work;
    }
    // Trial-level work ratio: total incumbent / total candidate.
    const auto d_total = dstar.work();
    if (d_total > 0) {
      ratios.push_back(static_cast<double>(incumbent_work) /
                       static_cast<double>(d_total));
    }
  }
}

}  // namespace

int main() {
  struct Cell {
    FamilyKind family;
    int size;
    bool route_local;
    int edits;
  };
  std::vector<double> all_ratios;
  for (const auto family : {FamilyKind::WallGap, FamilyKind::Rubble}) {
    for (const bool route_local : {true, false}) {
      for (const int edits : {1, 4, 16}) {
        std::vector<double> r64, r256;
        run_cell<64>(family, route_local, edits, r64);
        run_cell<256>(family, route_local, edits, r256);
        const auto med = [](std::vector<double> v) {
          std::sort(v.begin(), v.end());
          return v.empty() ? 0.0 : v[v.size() / 2];
        };
        std::printf(
            "p5 %-8s local=%d E=%-2d median ratio: 64=%0.3f 256=%0.3f\n",
            family_name(family), route_local ? 1 : 0, edits, med(r64),
            med(r256));
        all_ratios.insert(all_ratios.end(), r64.begin(), r64.end());
        all_ratios.insert(all_ratios.end(), r256.begin(), r256.end());
      }
    }
  }
  std::sort(all_ratios.begin(), all_ratios.end());
  const auto pooled = all_ratios.empty() ? 0.0
                                         : all_ratios[all_ratios.size() / 2];
  std::printf("\npooled median incumbent/candidate work ratio = %0.3f "
              "(stage-1 bar: >= 1.5)\n",
              pooled);
  std::printf("stage 1 -> %s\n", pooled >= 1.5 ? "PROCEED TO TIMING"
                                               : "REJECT WITHOUT HARDWARE");
  std::printf("%s\n", failures == 0 ? "ALL GATES PASSED" : "GATE FAILURES");
  return failures == 0 ? 0 : 1;
}
```

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

# P4 prototype: recorded source

The complete diff against main `35f3d53b` that the probes and both bench
arms were compiled from, all behind `-DTESS_P4_BIDIR` plus the
branch-only bench target (tracked via intent-to-add so the diff includes
it -- the omission P3's record needed an addendum for). With the macro
undefined the baseline is untouched. Trailing whitespace is stripped
from blank diff-context lines for the repository hook, so this diff
documents rather than reapplies.

```diff
diff --git a/bench/CMakeLists.txt b/bench/CMakeLists.txt
index 14022522..17457ec1 100644
--- a/bench/CMakeLists.txt
+++ b/bench/CMakeLists.txt
@@ -29,6 +29,13 @@ add_executable(
 target_link_libraries(tess_bench PRIVATE tess::tess benchmark::benchmark_main)
 tess_apply_project_options(tess_bench)

+# P4 screen target, branch-only, never merged: one source, two arms via
+# -DTESS_P4_BIDIR on the compile line of a separate build tree.
+add_executable(tess_p4_bidir_bench tess_p4_bidir_bench.cc)
+target_link_libraries(tess_p4_bidir_bench PRIVATE tess::tess
+                                                  benchmark::benchmark)
+tess_apply_project_options(tess_p4_bidir_bench)
+
 add_executable(
   tess_bench_diagnostics
   tess_bench.cc
diff --git a/bench/tess_p4_bidir_bench.cc b/bench/tess_p4_bidir_bench.cc
new file mode 100644
index 00000000..b10b3b9e
--- /dev/null
+++ b/bench/tess_p4_bidir_bench.cc
@@ -0,0 +1,180 @@
+// P4 screen benchmark, branch-only, never merged. One source builds both
+// arms: the base binary compiles without TESS_P4_BIDIR and runs the
+// incumbent search; the head binary compiles with it and opts every
+// query into the bidirectional arm. Families, sizes, seeds, and query draws
+// follow issue #251. Unlike the correctness probe (per-trial terrains,
+// one query each), each timed cell holds ONE terrain -- trial 0's -- and
+// draws 20 queries against it, so the paired timer compares identical
+// memory images; the counters program asserts equal reconstruct totals
+// between the arms on this exact workload, which pins cost consistency
+// here independently of the probe.
+#include <benchmark/benchmark.h>
+#include <tess/tess.h>
+
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
+enum class FamilyKind { Open, WallGap, Maze, Rubble };
+
+std::vector<std::uint8_t> build_terrain(FamilyKind family, int n,
+                                        std::uint64_t seed) {
+  std::vector<std::uint8_t> grid(static_cast<std::size_t>(n) * n, 1);
+  const auto close = [&](int x, int y) {
+    grid[static_cast<std::size_t>(y) * n + x] = 0;
+  };
+  switch (family) {
+    case FamilyKind::Open:
+      break;
+    case FamilyKind::WallGap: {
+      const int spacing = n / 8;
+      int k = 0;
+      for (int x = spacing; x < n - 1; x += spacing, ++k) {
+        const bool gap_high = (k % 2) == 0;
+        for (int y = 0; y < n; ++y) {
+          const bool in_gap = gap_high ? (y >= n - 2) : (y <= 1);
+          if (!in_gap) close(x, y);
+        }
+      }
+      break;
+    }
+    case FamilyKind::Maze: {
+      int k = 0;
+      for (int y = 4; y < n - 1; y += 4, ++k) {
+        const bool gap_right = (k % 2) == 0;
+        for (int x = 0; x < n; ++x) {
+          const bool in_gap = gap_right ? (x >= n - 2) : (x <= 1);
+          if (!in_gap) close(x, y);
+        }
+      }
+      break;
+    }
+    case FamilyKind::Rubble: {
+      Rng rng{seed};
+      for (int y = 0; y < n; ++y)
+        for (int x = 0; x < n; ++x)
+          if (rng.below(100) < 25) close(x, y);
+      break;
+    }
+  }
+  return grid;
+}
+
+template <int N>
+struct Workload {
+  using Shape = tess::Shape<tess::Extent3{N, N, 1}, tess::Extent3{16, 16, 1}>;
+  using World = tess::AlwaysResidentWorld<Shape, Schema>;
+
+  std::unique_ptr<World> world = std::make_unique<World>();
+  std::vector<tess::PathRequest> queries;
+
+  explicit Workload(FamilyKind family) {
+    // 20 pre-registered trials; each trial contributes its query pair,
+    // reachable or not (NoPath answers are part of the eligible
+    // workload). The world holds trial 0's terrain and every query runs
+    // against it -- unlike the correctness probe there is one terrain
+    // per family x size cell, so the paired timer compares identical
+    // memory images; trial variation lives in the query draw.
+    const auto seed = 0x9E3779B97F4A7C15ULL *
+                      (static_cast<std::uint64_t>(family) * 1000003ULL + 1);
+    const auto grid = build_terrain(family, N, seed);
+    for (auto& page : world->chunks()) {
+      auto open = page.template field_span<PassableTag>();
+      for (std::size_t i = 0; i < open.size(); ++i) open[i] = false;
+    }
+    std::vector<tess::Coord3> free;
+    for (int y = 0; y < N; ++y) {
+      for (int x = 0; x < N; ++x) {
+        if (grid[static_cast<std::size_t>(y) * N + x] != 0) {
+          world->template field<PassableTag>(tess::Coord3{x, y, 0}) = true;
+          free.push_back(tess::Coord3{x, y, 0});
+        }
+      }
+    }
+    Rng rng{seed ^ 0x5A5A5A5A5A5A5A5AULL};
+    for (unsigned trial = 0; trial < 20; ++trial) {
+      const auto s = free[rng.below(free.size())];
+      const auto g = free[rng.below(free.size())];
+      queries.push_back(tess::PathRequest{s, g});
+    }
+  }
+};
+
+template <int N, FamilyKind F>
+void run_cell(benchmark::State& state) {
+  // The workload is a function-local static, so the family must be a
+  // template parameter: a runtime argument would initialize one shared
+  // instance for whichever family ran first.
+  static const Workload<N> workload(F);
+  tess::PathScratch scratch;
+  scratch.reserve_nodes(static_cast<std::size_t>(N) * N);
+#ifdef TESS_P4_BIDIR
+  scratch.p4_bidir_ = true;
+#endif
+  for (auto _ : state) {
+    std::uint64_t sink = 0;
+    for (const auto& req : workload.queries) {
+      const auto r = tess::astar_path<typename Workload<N>::World, PassableTag>(
+          *workload.world, req, scratch,
+          tess::MissingChunkPolicy::ReportIndeterminate);
+      sink += r.cost + static_cast<std::uint64_t>(r.status);
+    }
+    benchmark::DoNotOptimize(sink);
+  }
+}
+
+void BM_p4_open_64(benchmark::State& state) {
+  run_cell<64, FamilyKind::Open>(state);
+}
+void BM_p4_open_256(benchmark::State& state) {
+  run_cell<256, FamilyKind::Open>(state);
+}
+void BM_p4_wall_gap_64(benchmark::State& state) {
+  run_cell<64, FamilyKind::WallGap>(state);
+}
+void BM_p4_wall_gap_256(benchmark::State& state) {
+  run_cell<256, FamilyKind::WallGap>(state);
+}
+void BM_p4_maze_64(benchmark::State& state) {
+  run_cell<64, FamilyKind::Maze>(state);
+}
+void BM_p4_maze_256(benchmark::State& state) {
+  run_cell<256, FamilyKind::Maze>(state);
+}
+void BM_p4_rubble_64(benchmark::State& state) {
+  run_cell<64, FamilyKind::Rubble>(state);
+}
+void BM_p4_rubble_256(benchmark::State& state) {
+  run_cell<256, FamilyKind::Rubble>(state);
+}
+
+BENCHMARK(BM_p4_open_64)->Name("p4/search_open_64");
+BENCHMARK(BM_p4_open_256)->Name("p4/search_open_256");
+BENCHMARK(BM_p4_wall_gap_64)->Name("p4/search_wall_gap_64");
+BENCHMARK(BM_p4_wall_gap_256)->Name("p4/search_wall_gap_256");
+BENCHMARK(BM_p4_maze_64)->Name("p4/search_maze_64");
+BENCHMARK(BM_p4_maze_256)->Name("p4/search_maze_256");
+BENCHMARK(BM_p4_rubble_64)->Name("p4/search_rubble_64");
+BENCHMARK(BM_p4_rubble_256)->Name("p4/search_rubble_256");
+
+}  // namespace
+
+BENCHMARK_MAIN();
diff --git a/include/tess/path/detail/astar.h b/include/tess/path/detail/astar.h
index 491d2bd9..60358ca7 100644
--- a/include/tess/path/detail/astar.h
+++ b/include/tess/path/detail/astar.h
@@ -8,6 +8,221 @@
 // (Slice 0 pre-split) to keep path.h under the 24k-token hook. Bare detail
 // fragment: included by path.h from inside namespace tess, never directly.

+#ifdef TESS_P4_BIDIR
+// P4 screen scaffolding, never merged. Whole-query bidirectional unit
+// search for the P3 domain (dense, orthogonal, DefaultSteps, unit cost,
+// 2D, no provider), behind a caller opt-in flag; the incumbent's entire
+// pre-search fast path is retained and only the exhaustive heap loop is
+// replaced. Two frontiers with Manhattan heuristics toward each other's
+// origins, alternating by open-list size; mu tracks the best meeting
+// cost, and the search terminates when mu <= max(minf_forward,
+// minf_backward): every undiscovered s-t path contains an open node in
+// each direction whose f bounds the path's length from below, so no
+// shorter path can remain. Cost optimality is oracle-gated per trial
+// rather than argued from this sketch.
+namespace detail {
+
+template <typename World, typename Tag>
+[[nodiscard]] auto p4_bidir_search(const World& world, PathRequest request,
+                                   PathScratch& scratch) -> PathResult {
+  using Shape = typename World::shape_type;
+  using Space = detail::NodeIndexSpace<World>;
+  static_assert(Space::is_dense);
+  constexpr auto infinite_cost = std::numeric_limits<std::uint32_t>::max();
+  constexpr auto no_parent = std::numeric_limits<std::uint64_t>::max();
+  const Space space{world};
+
+  // The backward direction owns a second node-array set of the same
+  // shape as the incumbent's, carried on the scratch behind the macro
+  // (the pre-registered memory bound: incumbent + one extra set).
+  const auto node_count = space.capacity_hint();
+  if (scratch.p4_g_.size() != node_count) {
+    scratch.p4_generation_.assign(node_count, 0);
+    scratch.p4_g_.assign(node_count, infinite_cost);
+    scratch.p4_parent_.assign(node_count, no_parent);
+  }
+  ++scratch.p4_epoch_;
+  if (scratch.p4_epoch_ == 0) {
+    std::fill(scratch.p4_generation_.begin(), scratch.p4_generation_.end(),
+              0);
+    scratch.p4_epoch_ = 1;
+  }
+  scratch.p4_open_.clear();
+
+  const auto passable = [&world](Coord3 c) {
+    return contains<Shape>(c) && detail::is_passable<World, Tag>(world, c);
+  };
+  const auto manhattan = [](Coord3 a, Coord3 b) -> std::uint32_t {
+    const auto dx = a.x > b.x ? a.x - b.x : b.x - a.x;
+    const auto dy = a.y > b.y ? a.y - b.y : b.y - a.y;
+    return static_cast<std::uint32_t>(dx + dy);
+  };
+
+  // Forward state lives in the primary scratch arrays (epoch-checked
+  // through g_at); backward state in the p4_ set with its own epoch.
+  const auto bwd_g_at = [&scratch, infinite_cost](std::size_t offset) {
+    return scratch.p4_generation_[offset] == scratch.p4_epoch_
+               ? scratch.p4_g_[offset]
+               : infinite_cost;
+  };
+  const auto bwd_touch = [&scratch](std::size_t offset) {
+    scratch.p4_generation_[offset] = scratch.p4_epoch_;
+  };
+
+  const auto heap_less = [](detail::PackedOpenNode lhs,
+                            detail::PackedOpenNode rhs) {
+    return lhs.key > rhs.key;
+  };
+  auto& fwd_open = scratch.open_;
+  auto& bwd_open = scratch.p4_open_;
+
+  const auto start_coord = request.start;
+  const auto goal_coord = request.goal;
+  const auto start_index = detail::tile_index<Shape>(start_coord);
+  const auto goal_index = detail::tile_index<Shape>(goal_coord);
+
+  std::uint32_t mu = infinite_cost;
+  std::uint64_t meet_index = no_parent;
+
+  {
+    const auto so = space.offset(start_index);
+    scratch.g_[so] = 0;
+    scratch.touch_node(so);
+    TESS_DIAG_EVENT(path_touch_node);
+    scratch.parent_[so] = no_parent;
+    fwd_open.push_back(detail::PackedOpenNode::make(
+        start_index, 0, manhattan(start_coord, goal_coord)));
+    std::push_heap(fwd_open.begin(), fwd_open.end(), heap_less);
+    TESS_DIAG_EVENT(path_heap_push);
+    const auto go = space.offset(goal_index);
+    bwd_touch(go);
+    scratch.p4_g_[go] = 0;
+    scratch.p4_parent_[go] = no_parent;
+    bwd_open.push_back(detail::PackedOpenNode::make(
+        goal_index, 0, manhattan(goal_coord, start_coord)));
+    std::push_heap(bwd_open.begin(), bwd_open.end(), heap_less);
+    TESS_DIAG_EVENT(path_heap_push);
+    if (start_index == goal_index) {
+      scratch.path_.push_back(start_coord);
+      return PathResult{PathStatus::Found, 0, 0, scratch.touched_count_,
+                        scratch.path_};
+    }
+    // A meeting can happen at seeding when start and goal are adjacent;
+    // the loop's relax handles it like any other cross-direction hit.
+  }
+
+  std::size_t expanded_nodes = 0;
+  const auto expand_one = [&](bool forward) {
+    auto& open = forward ? fwd_open : bwd_open;
+    std::pop_heap(open.begin(), open.end(), heap_less);
+    const auto entry = open.back();
+    open.pop_back();
+    TESS_DIAG_EVENT(path_heap_pop);
+    const auto index = entry.index;
+    const auto offset = space.offset(index);
+    const auto entry_g = entry.g();
+    const auto known =
+        forward ? scratch.g_at(offset, infinite_cost) : bwd_g_at(offset);
+    if (entry_g != known) {
+      TESS_DIAG_EVENT_VALUE(path_skip_pop, false);
+      return;
+    }
+    ++expanded_nodes;
+    const auto coord = detail::tile_coord<Shape>(index);
+    const auto target_coord = forward ? goal_coord : start_coord;
+    const std::array<Coord3, 4> steps = {
+        Coord3{coord.x + 1, coord.y, 0}, Coord3{coord.x - 1, coord.y, 0},
+        Coord3{coord.x, coord.y + 1, 0}, Coord3{coord.x, coord.y - 1, 0}};
+    for (const auto step : steps) {
+      TESS_DIAG_EVENT(path_neighbor_candidate);
+      TESS_DIAG_EVENT(path_passability_check);
+      if (!passable(step)) {
+        continue;
+      }
+      const auto nindex = detail::tile_index<Shape>(step);
+      const auto noffset = space.offset(nindex);
+      const auto tentative = entry_g + 1;
+      TESS_DIAG_EVENT(path_relax_attempt);
+      const auto nknown = forward ? scratch.g_at(noffset, infinite_cost)
+                                  : bwd_g_at(noffset);
+      if (tentative < nknown) {
+        TESS_DIAG_EVENT(path_relax_success);
+        if (forward) {
+          if (nknown == infinite_cost) {
+            scratch.touch_node(noffset);
+            TESS_DIAG_EVENT(path_touch_node);
+          }
+          scratch.g_[noffset] = tentative;
+          scratch.parent_[noffset] = index;
+        } else {
+          if (nknown == infinite_cost) {
+            bwd_touch(noffset);
+            TESS_DIAG_EVENT(path_touch_node);
+          }
+          scratch.p4_g_[noffset] = tentative;
+          scratch.p4_parent_[noffset] = index;
+        }
+        TESS_DIAG_EVENT(path_heuristic);
+        const auto f = detail::saturating_add(
+            tentative, manhattan(step, target_coord));
+        auto& push_open = forward ? fwd_open : bwd_open;
+        push_open.push_back(
+            detail::PackedOpenNode::make(nindex, tentative, f));
+        std::push_heap(push_open.begin(), push_open.end(), heap_less);
+        TESS_DIAG_EVENT(path_heap_push);
+      }
+      // Cross-direction meeting check on every relax attempt: the other
+      // side's settled-or-open g plus ours bounds a full route.
+      const auto other_g =
+          forward ? bwd_g_at(noffset) : scratch.g_at(noffset, infinite_cost);
+      if (other_g != infinite_cost) {
+        const auto total = detail::saturating_add(tentative, other_g);
+        if (total < mu) {
+          mu = total;
+          meet_index = nindex;
+        }
+      }
+    }
+  };
+
+  while (!fwd_open.empty() && !bwd_open.empty()) {
+    const auto fwd_top_f = fwd_open.front().f();
+    const auto bwd_top_f = bwd_open.front().f();
+    if (mu <= (fwd_top_f > bwd_top_f ? fwd_top_f : bwd_top_f)) {
+      break;
+    }
+    expand_one(fwd_open.size() <= bwd_open.size());
+  }
+
+  if (mu == infinite_cost) {
+    return PathResult{PathStatus::NoPath, 0, expanded_nodes,
+                      scratch.touched_count_, scratch.path_};
+  }
+
+  // Reconstruct: forward chain start->meet, then backward chain
+  // meet->goal (backward parents point toward the goal).
+  {
+    auto cur = meet_index;
+    while (cur != no_parent) {
+      scratch.path_.push_back(detail::tile_coord<Shape>(cur));
+      TESS_DIAG_EVENT(path_reconstruct_node);
+      cur = scratch.parent_[space.offset(cur)];
+    }
+    std::reverse(scratch.path_.begin(), scratch.path_.end());
+    auto bwd = scratch.p4_parent_[space.offset(meet_index)];
+    while (bwd != no_parent) {
+      scratch.path_.push_back(detail::tile_coord<Shape>(bwd));
+      TESS_DIAG_EVENT(path_reconstruct_node);
+      bwd = scratch.p4_parent_[space.offset(bwd)];
+    }
+  }
+  return PathResult{PathStatus::Found, mu, expanded_nodes,
+                    scratch.touched_count_, scratch.path_};
+}
+
+}  // namespace detail
+#endif  // TESS_P4_BIDIR
+
 /// Finds a minimum-step path through tiles where field `Tag` is truthy.
 ///
 /// The returned path borrows `scratch` until its next mutation. Sparse-world
@@ -625,6 +840,18 @@ template <typename World, typename Tag>
     scratch.parent_.assign(node_count, no_parent);
   }

+#ifdef TESS_P4_BIDIR
+  // Compile-time gate, P3-precedent: density and dimensionality decide
+  // here; non-orthogonal, scaled-cost, and provider models were already
+  // routed to the weighted core above. Ineligible worlds ignore the
+  // flag and take the incumbent loop byte-identically.
+  if constexpr (Space::is_dense && ShapeTraits<Shape>::degenerate_z &&
+                !Model::has_special_transitions) {
+    if (scratch.p4_bidir_) {
+      return detail::p4_bidir_search<World, Tag>(world, request, scratch);
+    }
+  }
+#endif
   const auto start = detail::tile_index<Shape>(request.start);
   const auto goal = detail::tile_index<Shape>(request.goal);
   const auto start_offset = space.offset(start);
diff --git a/include/tess/path/path.h b/include/tess/path/path.h
index 80228c80..ba271f21 100644
--- a/include/tess/path/path.h
+++ b/include/tess/path/path.h
@@ -85,6 +85,13 @@ struct WeightedPathBatchStats {

 /// Owns reusable node and result storage for A* queries.
 class PathScratch;
+#ifdef TESS_P4_BIDIR
+namespace detail {
+template <typename World, typename Tag>
+[[nodiscard]] auto p4_bidir_search(const World& world, PathRequest request,
+                                   PathScratch& scratch) -> PathResult;
+}  // namespace detail
+#endif
 /// Owns reusable node and path storage for distance-field queries.
 class DistanceFieldScratch;
 /// Owns the ordered goals for a multi-goal distance product.
@@ -820,6 +827,25 @@ class PathScratch {
     ++touched_count_;
   }

+#ifdef TESS_P4_BIDIR
+  // P4 screen scaffolding, never merged: the bidirectional arm reuses
+  // this scratch for its forward direction and carries the backward
+  // direction's node arrays here (the pre-registered memory bound).
+  template <typename World, typename Tag>
+  friend auto detail::p4_bidir_search(const World& world, PathRequest request,
+                                      PathScratch& scratch) -> PathResult;
+
+ public:
+  bool p4_bidir_ = false;
+  std::vector<std::uint32_t> p4_generation_;
+  std::uint32_t p4_epoch_ = 1;
+  std::vector<std::uint32_t> p4_g_;
+  std::vector<std::uint64_t> p4_parent_;
+  std::vector<detail::PackedOpenNode> p4_open_;
+
+ private:
+#endif
+
   std::vector<detail::PackedOpenNode> open_;
   std::vector<detail::PackedOpenNode> open_next_;
   // Parallel arrays deliberately: an interleaved {generation, g, state}

```

# P3 prototype: recorded source

The complete diff against main `f16783b7` that every recorded probe and
both bench arms were compiled from, all behind `-DTESS_P3_JPS` (plus the
branch-only bench target). With the macro undefined the dev suite passes
unchanged (1592/1592), so the two bench binaries differ only by this
text. Trailing whitespace is stripped from blank diff-context lines to
satisfy the repository whitespace hook, so this diff documents rather
than reapplies.

```diff
diff --git a/bench/CMakeLists.txt b/bench/CMakeLists.txt
index 14022522..448caa9e 100644
--- a/bench/CMakeLists.txt
+++ b/bench/CMakeLists.txt
@@ -29,6 +29,13 @@ add_executable(
 target_link_libraries(tess_bench PRIVATE tess::tess benchmark::benchmark_main)
 tess_apply_project_options(tess_bench)

+# P3 screen target, branch-only, never merged: one source, two arms via
+# -DTESS_P3_JPS on the compile line of a separate build tree.
+add_executable(tess_p3_jps_bench tess_p3_jps_bench.cc)
+target_link_libraries(tess_p3_jps_bench PRIVATE tess::tess
+                                                benchmark::benchmark)
+tess_apply_project_options(tess_p3_jps_bench)
+
 add_executable(
   tess_bench_diagnostics
   tess_bench.cc
diff --git a/include/tess/path/detail/astar.h b/include/tess/path/detail/astar.h
index 491d2bd9..0002e377 100644
--- a/include/tess/path/detail/astar.h
+++ b/include/tess/path/detail/astar.h
@@ -8,6 +8,297 @@
 // (Slice 0 pre-split) to keep path.h under the 24k-token hook. Bare detail
 // fragment: included by path.h from inside namespace tess, never directly.

+#ifdef TESS_P3_JPS
+// P3 screen scaffolding, never merged. 4-connected Jump Point Search for
+// the gated domain (dense, orthogonal, DefaultSteps, unit cost, 2D, no
+// provider). The classic 8-connected algorithm does not apply without
+// diagonal moves; this is the 4-connected variant: straight scans that
+// stop at the goal, at goal-axis alignment (the canonical L-turn), or at
+// a forced neighbor -- a perpendicular tile that is open here but whose
+// equal-cost alternative through the parent's side is blocked, making
+// this tile the only optimal gateway.
+//
+// Cost-exactness stance: nodes are keyed by tile, but a tile can carry
+// different successor sets per incoming direction, so instead of a
+// closed set the loop expands EVERY pop whose g still equals the node's
+// best (a tile re-expands at most once per incoming direction, bounding
+// the extra work at 4x pops worst case, in exchange for not having to
+// prove the per-tile-closure variant complete). Route byte-identity with
+// the incumbent is declared out of scope in the pre-registration; cost
+// equality is oracle-gated per trial.
+namespace detail {
+
+template <typename World, typename Tag>
+[[nodiscard]] auto p3_jps_search(const World& world, PathRequest request,
+                                 PathScratch& scratch) -> PathResult {
+  using Shape = typename World::shape_type;
+  using Space = detail::NodeIndexSpace<World>;
+  static_assert(Space::is_dense);
+  // Incoming direction rides in the top bits of the packed node index.
+  static_assert(ShapeTraits<Shape>::tile_key_bits <= 61,
+                "P3 screen packs a 3-bit direction into the node index");
+  constexpr auto infinite_cost = std::numeric_limits<std::uint32_t>::max();
+  constexpr std::uint64_t dir_shift = 61;
+  constexpr std::uint64_t index_mask = (std::uint64_t{1} << dir_shift) - 1U;
+
+  const Space space{world};
+  const auto goal_coord = request.goal;
+  const auto start_coord = request.start;
+  const auto passable = [&world](Coord3 c) {
+    return contains<Shape>(c) && detail::is_passable<World, Tag>(world, c);
+  };
+  const auto manhattan = [&](Coord3 c) -> std::uint32_t {
+    const auto dx = c.x > goal_coord.x ? c.x - goal_coord.x : goal_coord.x - c.x;
+    const auto dy = c.y > goal_coord.y ? c.y - goal_coord.y : goal_coord.y - c.y;
+    return static_cast<std::uint32_t>(dx + dy);
+  };
+  // Directions: 1:+x 2:-x 3:+y 4:-y (0 = start, all four allowed).
+  const auto dir_dx = [](std::uint64_t d) -> std::int64_t {
+    return d == 1 ? 1 : d == 2 ? -1 : 0;
+  };
+  const auto dir_dy = [](std::uint64_t d) -> std::int64_t {
+    return d == 3 ? 1 : d == 4 ? -1 : 0;
+  };
+
+  // Two scan kinds, mirroring 8-connected JPS's diagonal-primary
+  // recursion with the horizontal axis as primary (canonical order:
+  // horizontal movement before vertical within any reorderable
+  // segment). A VERTICAL scan is secondary: it stops only at the goal
+  // tile itself or at a forced horizontal neighbor -- a tile open here
+  // whose equal-cost alternative through the parent's side is blocked.
+  // It deliberately does NOT stop on mere goal-row alignment: a
+  // vertical-then-horizontal L is the reorderable mirror of the
+  // horizontal-then-vertical canonical L that hjump's goal-column stop
+  // already generates, and stopping on row alignment made every column
+  // "interesting" to the probes below, collapsing the search to
+  // per-tile expansion. A HORIZONTAL scan additionally stops wherever a
+  // vertical scan launched from the current tile would find something,
+  // exactly as a diagonal scan in 8-connected JPS probes its straight
+  // rays; without the probes, mid-row turn points with no locally
+  // forced neighbor are unreachable and the search goes incomplete
+  // (caught by the oracle on the rubble family).
+  const auto vjump = [&](Coord3 from, std::int64_t dy) -> Coord3 {
+    auto p = from;
+    while (true) {
+      p.y += dy;
+      TESS_DIAG_EVENT(path_neighbor_candidate);
+      TESS_DIAG_EVENT(path_passability_check);
+      if (!passable(p)) {
+        return Coord3{-1, -1, 0};
+      }
+      if (p.y == goal_coord.y && p.x == goal_coord.x) {
+        return p;  // the goal tile itself
+      }
+      if ((!passable(Coord3{p.x + 1, p.y - dy, 0}) &&
+           passable(Coord3{p.x + 1, p.y, 0})) ||
+          (!passable(Coord3{p.x - 1, p.y - dy, 0}) &&
+           passable(Coord3{p.x - 1, p.y, 0}))) {
+        return p;  // forced horizontal neighbor
+      }
+    }
+  };
+  const auto hjump = [&](Coord3 from, std::int64_t dx) -> Coord3 {
+    auto p = from;
+    while (true) {
+      p.x += dx;
+      TESS_DIAG_EVENT(path_neighbor_candidate);
+      TESS_DIAG_EVENT(path_passability_check);
+      if (!passable(p)) {
+        return Coord3{-1, -1, 0};
+      }
+      if (p.x == goal_coord.x) {
+        return p;  // goal column (covers the goal tile itself)
+      }
+      if ((!passable(Coord3{p.x - dx, p.y + 1, 0}) &&
+           passable(Coord3{p.x, p.y + 1, 0})) ||
+          (!passable(Coord3{p.x - dx, p.y - 1, 0}) &&
+           passable(Coord3{p.x, p.y - 1, 0}))) {
+        return p;  // forced vertical neighbor
+      }
+      if (vjump(p, 1).x >= 0 || vjump(p, -1).x >= 0) {
+        return p;  // a vertical branch from here reaches something
+      }
+    }
+  };
+  const auto jump = [&](Coord3 from, std::int64_t dx,
+                        std::int64_t dy) -> Coord3 {
+    return dx != 0 ? hjump(from, dx) : vjump(from, dy);
+  };
+
+  const auto heap_less = [](detail::PackedOpenNode lhs,
+                            detail::PackedOpenNode rhs) {
+    return lhs.key > rhs.key;  // min-heap by (f, then deepest g)
+  };
+  auto& heap = scratch.open_;
+
+  const auto push = [&](Coord3 c, std::uint32_t g, std::uint64_t dir,
+                        std::uint64_t parent_index) {
+    const auto index = detail::tile_index<Shape>(c);
+    const auto offset = space.offset(index);
+    const auto known = scratch.g_at(offset, infinite_cost);
+    if (g > known) {
+      return;
+    }
+    if (g < known) {
+      // Equal-g re-arrivals keep the first parent (any optimal parent
+      // reconstructs an optimal route); strictly better g replaces it
+      // and reopens the tile's per-direction closed mask below.
+      if (known == infinite_cost) {
+        scratch.touch_node(offset);
+        TESS_DIAG_EVENT(path_touch_node);
+      }
+      scratch.g_[offset] = g;
+      scratch.parent_[offset] = parent_index;
+      scratch.state_[offset] = 0;
+    }
+    TESS_DIAG_EVENT(path_heuristic);
+    const auto f = detail::saturating_add(g, manhattan(c));
+    heap.push_back(
+        detail::PackedOpenNode::make((dir << dir_shift) | index, g, f));
+    std::push_heap(heap.begin(), heap.end(), heap_less);
+    TESS_DIAG_EVENT(path_heap_push);
+  };
+
+  const auto start_index = detail::tile_index<Shape>(start_coord);
+  const auto goal_index = detail::tile_index<Shape>(goal_coord);
+  const auto start_offset = space.offset(start_index);
+  scratch.g_[start_offset] = 0;
+  scratch.touch_node(start_offset);
+  scratch.state_[start_offset] = 0;
+  scratch.parent_[start_offset] = start_index;
+  TESS_DIAG_EVENT(path_touch_node);
+  heap.push_back(detail::PackedOpenNode::make(start_index, 0, manhattan(start_coord)));
+  std::push_heap(heap.begin(), heap.end(), heap_less);
+  TESS_DIAG_EVENT(path_heap_push);
+
+  std::size_t expanded_nodes = 0;
+  while (!heap.empty()) {
+    std::pop_heap(heap.begin(), heap.end(), heap_less);
+    const auto entry = heap.back();
+    heap.pop_back();
+    TESS_DIAG_EVENT(path_heap_pop);
+    const auto packed = entry.index;
+    const auto index = packed & index_mask;
+    const auto dir = packed >> dir_shift;
+    const auto offset = space.offset(index);
+    const auto entry_g = entry.g();
+    if (entry_g != scratch.g_at(offset, infinite_cost)) {
+      TESS_DIAG_EVENT_VALUE(path_skip_pop, false);
+      continue;  // superseded by a better arrival
+    }
+    // Closed per (tile, incoming direction): equal-g re-arrivals of the
+    // same direction regenerate the same successors, and without this
+    // mask a cycle of equal-g jump points re-expands forever. A strictly
+    // better g reopens the mask (see push). state_ doubles as the mask;
+    // its generation currency rides on g_'s touch.
+    const auto dir_bit = static_cast<std::uint8_t>(1U << dir);
+    if ((scratch.state_at(offset, std::uint8_t{0}) & dir_bit) != 0) {
+      TESS_DIAG_EVENT_VALUE(path_skip_pop, true);
+      continue;
+    }
+    scratch.state_[offset] =
+        static_cast<std::uint8_t>(scratch.state_at(offset, std::uint8_t{0}) |
+                                  dir_bit);
+    const auto coord = detail::tile_coord<Shape>(index);
+    ++expanded_nodes;
+
+    if (index == goal_index) {
+      // Reconstruct through the jump-point chain, expanding each straight
+      // segment into the per-tile route PathResult promises.
+      auto cur = index;
+      while (true) {
+        const auto cur_coord = detail::tile_coord<Shape>(cur);
+        scratch.path_.push_back(cur_coord);
+        TESS_DIAG_EVENT(path_reconstruct_node);
+        if (cur == start_index) {
+          break;
+        }
+        const auto parent = scratch.parent_[space.offset(cur)];
+        const auto parent_coord = detail::tile_coord<Shape>(parent);
+        const auto sx = parent_coord.x == cur_coord.x
+                            ? std::int64_t{0}
+                            : (parent_coord.x > cur_coord.x ? 1 : -1);
+        const auto sy = parent_coord.y == cur_coord.y
+                            ? std::int64_t{0}
+                            : (parent_coord.y > cur_coord.y ? 1 : -1);
+        auto walk = cur_coord;
+        while (true) {
+          walk.x += sx;
+          walk.y += sy;
+          if (walk.x == parent_coord.x && walk.y == parent_coord.y) {
+            break;
+          }
+          scratch.path_.push_back(walk);
+          TESS_DIAG_EVENT(path_reconstruct_node);
+        }
+        cur = parent;
+      }
+      std::reverse(scratch.path_.begin(), scratch.path_.end());
+      return PathResult{PathStatus::Found, entry_g, expanded_nodes,
+                        scratch.touched_count_, scratch.path_};
+    }
+
+    // Successor directions: continue straight, plus forced perpendiculars,
+    // plus the canonical turn toward the goal on axis alignment. The start
+    // (dir 0) scans all four.
+    std::uint64_t dirs[4];
+    std::size_t dir_count = 0;
+    if (dir == 0) {
+      dirs[dir_count++] = 1;
+      dirs[dir_count++] = 2;
+      dirs[dir_count++] = 3;
+      dirs[dir_count++] = 4;
+    } else if (dir_dx(dir) != 0) {
+      // Horizontal is primary: continue, and both vertical branches --
+      // the scan only stopped here because one of them (or a forced or
+      // goal-column condition) has somewhere to go.
+      dirs[dir_count++] = dir;
+      dirs[dir_count++] = 3;
+      dirs[dir_count++] = 4;
+    } else {
+      // Vertical is secondary: continue, forced horizontal branches,
+      // and the canonical L-turn on the goal row.
+      dirs[dir_count++] = dir;
+      const auto dy = dir_dy(dir);
+      if (!passable(Coord3{coord.x + 1, coord.y - dy, 0}) &&
+          passable(Coord3{coord.x + 1, coord.y, 0})) {
+        dirs[dir_count++] = 1;
+      }
+      if (!passable(Coord3{coord.x - 1, coord.y - dy, 0}) &&
+          passable(Coord3{coord.x - 1, coord.y, 0})) {
+        dirs[dir_count++] = 2;
+      }
+      if (coord.y == goal_coord.y && goal_coord.x != coord.x) {
+        const std::uint64_t toward = goal_coord.x > coord.x ? 1 : 2;
+        bool present = false;
+        for (std::size_t i = 0; i < dir_count; ++i) {
+          present |= dirs[i] == toward;
+        }
+        if (!present) {
+          dirs[dir_count++] = toward;
+        }
+      }
+    }
+    for (std::size_t i = 0; i < dir_count; ++i) {
+      const auto d = dirs[i];
+      const auto target = jump(coord, dir_dx(d), dir_dy(d));
+      if (target.x < 0) {
+        continue;
+      }
+      const auto step_cost = static_cast<std::uint32_t>(
+          (target.x > coord.x ? target.x - coord.x : coord.x - target.x) +
+          (target.y > coord.y ? target.y - coord.y : coord.y - target.y));
+      TESS_DIAG_EVENT(path_relax_attempt);
+      push(target, entry_g + step_cost, d, index);
+    }
+  }
+  return PathResult{PathStatus::NoPath, 0, expanded_nodes,
+                    scratch.touched_count_, scratch.path_};
+}
+
+}  // namespace detail
+#endif  // TESS_P3_JPS
+
 /// Finds a minimum-step path through tiles where field `Tag` is truthy.
 ///
 /// The returned path borrows `scratch` until its next mutation. Sparse-world
@@ -625,6 +916,19 @@ template <typename World, typename Tag>
     scratch.parent_.assign(node_count, no_parent);
   }

+#ifdef TESS_P3_JPS
+  // The gate is compile-time traits only, per the pre-registration; the
+  // unit entry point has already routed non-orthogonal, scaled-cost, and
+  // provider models to the weighted core above, so what remains here is
+  // density and dimensionality. An ineligible world ignores the flag and
+  // takes the incumbent loop below, byte-identically.
+  if constexpr (Space::is_dense && ShapeTraits<Shape>::degenerate_z &&
+                !Model::has_special_transitions) {
+    if (scratch.p3_jps_) {
+      return detail::p3_jps_search<World, Tag>(world, request, scratch);
+    }
+  }
+#endif
   const auto start = detail::tile_index<Shape>(request.start);
   const auto goal = detail::tile_index<Shape>(request.goal);
   const auto start_offset = space.offset(start);
diff --git a/include/tess/path/path.h b/include/tess/path/path.h
index 80228c80..90a9b402 100644
--- a/include/tess/path/path.h
+++ b/include/tess/path/path.h
@@ -85,6 +85,13 @@ struct WeightedPathBatchStats {

 /// Owns reusable node and result storage for A* queries.
 class PathScratch;
+#ifdef TESS_P3_JPS
+namespace detail {
+template <typename World, typename Tag>
+[[nodiscard]] auto p3_jps_search(const World& world, PathRequest request,
+                                 PathScratch& scratch) -> PathResult;
+}  // namespace detail
+#endif
 /// Owns reusable node and path storage for distance-field queries.
 class DistanceFieldScratch;
 /// Owns the ordered goals for a multi-goal distance product.
@@ -820,6 +827,20 @@ class PathScratch {
     ++touched_count_;
   }

+#ifdef TESS_P3_JPS
+  // P3 screen scaffolding, never merged: the JPS arm reuses this scratch.
+  template <typename World, typename Tag>
+  friend auto detail::p3_jps_search(const World& world, PathRequest request,
+                                    PathScratch& scratch) -> PathResult;
+
+ public:
+  // Caller opt-in for the gated JPS arm; ignored (detectably: the route
+  // is byte-identical to the incumbent's) outside the eligible domain.
+  bool p3_jps_ = false;
+
+ private:
+#endif
+
   std::vector<detail::PackedOpenNode> open_;
   std::vector<detail::PackedOpenNode> open_next_;
   // Parallel arrays deliberately: an interleaved {generation, g, state}

```

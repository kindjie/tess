# P2 prototype: recorded source

The resumable-search scaffolding, never merged: the complete diff against
main `e235fe05` that the three recorded probes were compiled against, all
behind `-DTESS_P2_RESUMABLE`. With the macro undefined the full dev suite
passes unchanged (1585/1585), so the baseline arm and the prototype arm
differ only by this text.

```diff
diff --git a/include/tess/path/detail/astar.h b/include/tess/path/detail/astar.h
index 491d2bd9..56682e1d 100644
--- a/include/tess/path/detail/astar.h
+++ b/include/tess/path/detail/astar.h
@@ -12,11 +12,92 @@
 ///
 /// The returned path borrows `scratch` until its next mutation. Sparse-world
 /// resident boundaries follow `policy`.
+#ifdef TESS_P2_RESUMABLE
+// P2 screen scaffolding, never merged. Caller-owned continuation state for
+// one paused query.
+//
+// The scratch arrays already live in a caller-reachable PathScratch, so the
+// only state a pause loses is the handful of loop locals below. Keeping the
+// single implementation rather than copying it is deliberate: it makes
+// expansion equality between a contiguous and a resumed run structural rather
+// than something the screen has to measure and hope for.
+//
+// The dense fast-path preamble is an ATOMIC slice 0. It produces no
+// expansions, can be O(extent^2), and often returns the whole route with its
+// own stitching order, so route identity forces including it and including it
+// means the per-slice bound covers heap-loop work only. That limit is the
+// screen's, not a defect.
+struct P2Slice {
+  // Expansions permitted per call. Zero means unbounded.
+  std::size_t budget = 0;
+  // Set once the preamble has run and the loop state is live.
+  bool started = false;
+  // Loop locals that a pause would otherwise drop.
+  std::uint32_t current_f = 0;
+  std::size_t expanded_nodes = 0;
+  bool crossed_missing = false;
+  // Counts calls, so a test can assert a schedule actually sliced.
+  std::size_t slices = 0;
+  // True when the last call returned at the slice boundary rather than
+  // terminating. The boundary return borrows PathStatus::Indeterminate,
+  // which a sparse search also uses legitimately, so the caller needs an
+  // unambiguous signal rather than inferring one from the status.
+  bool paused = false;
+
+  // One captured chunk dependency: the content version at first read, and
+  // for sparse worlds the residency generation at first read. Generation
+  // zero means the chunk was NOT resident at that moment; a later
+  // materialization changes a conclusion this search already drew (a
+  // skipped neighbor, an impassable read), so the single rule "either
+  // recorded value moved => stale" uniformly covers content edits,
+  // eviction, rematerialization (whose content version restarts at zero),
+  // slot-aliasing corruption, and absent-becomes-resident.
+  struct Dep {
+    ChunkKey key{};
+    ContentVersion content_version{};
+    ResidencyGeneration residency_generation{};
+  };
+  std::vector<Dep> deps;
+  // Dedupe memo: consecutive reads cluster in one chunk, so most probes
+  // are answered here without scanning deps.
+  static constexpr std::uint64_t no_memo =
+      std::numeric_limits<std::uint64_t>::max();
+  std::uint64_t last_key = no_memo;
+  // Set when resume-time revalidation found a captured dependency changed
+  // (or the caller resumed with a different request). The query cannot
+  // continue; the caller resets and reruns. Distinct from paused, which
+  // invites another call.
+  bool stale = false;
+  // Request fingerprint taken on the first call. Resuming with a different
+  // request is caller misuse and is refused in every build (asserts
+  // compile out of release) rather than silently searching a hybrid query.
+  Coord3 start{};
+  Coord3 goal{};
+  // Screen accounting, declared in the pre-registration amendment: every
+  // reported chunk key during capture and every captured entry compared at
+  // a resume count toward the resumed arm's non-expansion total. The
+  // contiguous nullptr baseline pays neither.
+  std::size_t capture_probes = 0;
+  std::size_t revalidation_checks = 0;
+
+  // Cancellation: forget the in-flight query but keep the deps capacity, so
+  // reuse for a later query neither resumes the dead one nor reallocates.
+  void reset() noexcept {
+    started = false;
+    paused = false;
+    stale = false;
+  }
+};
+#endif
+
 template <typename World, typename Tag>
 [[nodiscard]] auto astar_path(const World& world, PathRequest request,
                               PathScratch& scratch,
                               [[maybe_unused]] MissingChunkPolicy policy)
     -> PathResult {
+#ifdef TESS_P2_RESUMABLE
+  P2Slice* const slice = scratch.p2_slice_;
+#endif
   using Shape = typename World::shape_type;
   using Space = detail::NodeIndexSpace<World>;
   using Class = movement::movement_class_of<Tag>;
@@ -43,8 +124,119 @@ template <typename World, typename Tag>
                                                  policy);
   }

-  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_count_);
-  scratch.clear();
+#ifdef TESS_P2_RESUMABLE
+  // Computed at entry because it gates the scratch reset and the whole
+  // dense preamble below, not merely the frontier seeding.
+  const bool p2_resuming = slice != nullptr && slice->started;
+  if (p2_resuming) {
+    // Misuse fails fast in every build: refuse rather than silently
+    // continuing the old search under the new request's arguments.
+    if (request.start != slice->start || request.goal != slice->goal) {
+      TESS_ASSERT_MSG(false, "P2 resume with a different request");
+      slice->stale = true;
+      slice->paused = false;
+      return PathResult{PathStatus::Indeterminate, 0, 0, 0, PathView{}};
+    }
+    if (slice->stale) {
+      // Still stale from an earlier refusal; the caller must reset().
+      return PathResult{PathStatus::Indeterminate, 0, 0, 0, PathView{}};
+    }
+    // Revalidate the captured dependency set BEFORE any scratch read and
+    // before the entry checks re-read live world state. On sparse worlds a
+    // changed residency generation means the node arrays may alias a
+    // different chunk's tiles under the current epoch -- corruption, not
+    // staleness -- so nothing below may run once a mismatch is possible.
+    for (const auto& dep : slice->deps) {
+      ++slice->revalidation_checks;
+      ContentVersion content{};
+      ResidencyGeneration generation{};
+      if constexpr (Space::is_dense) {
+        content = world.meta(dep.key).content_version;
+      } else {
+        if (const auto* meta = world.try_meta(dep.key)) {
+          content = meta->content_version;
+        }
+        generation = world.residency_generation(dep.key);
+      }
+      if (content != dep.content_version ||
+          generation != dep.residency_generation) {
+        slice->stale = true;
+        slice->paused = false;
+        return PathResult{PathStatus::Indeterminate, 0,
+                          slice->expanded_nodes, scratch.touched_count_,
+                          PathView{}};
+      }
+    }
+  } else if (slice != nullptr) {
+    // First call of a query: forget anything a previous query on this state
+    // object captured, so a cancelled, stale, or preamble-answered
+    // predecessor cannot leak captured versions into this query's
+    // revalidation. Capacity is retained (allocation gate).
+    slice->deps.clear();
+    // Cold-path reserve so warm slices do not allocate when the frontier
+    // enters a new chunk. Dense worlds can touch every chunk; a sparse
+    // query's captured set is the fixed resident set (any residency change
+    // kills the query as stale) plus its non-resident perimeter, for which
+    // 2x capacity is headroom, not a bound -- growth past it reallocates
+    // and the screen records whether that happened.
+    if constexpr (Space::is_dense) {
+      slice->deps.reserve(static_cast<std::size_t>(
+          std::min<std::uint64_t>(ShapeTraits<Shape>::chunk_count, 4096)));
+    } else {
+      slice->deps.reserve(world.capacity() * 2);
+    }
+    slice->last_key = P2Slice::no_memo;
+    slice->stale = false;
+    slice->paused = false;
+    slice->slices = 0;
+    slice->capture_probes = 0;
+    slice->revalidation_checks = 0;
+    slice->start = request.start;
+    slice->goal = request.goal;
+  }
+  const auto p2_record = [&world, slice](ChunkKey key) noexcept {
+    ++slice->capture_probes;
+    if (key.value == slice->last_key) {
+      return;
+    }
+    slice->last_key = key.value;
+    for (const auto& dep : slice->deps) {
+      if (dep.key == key) {
+        return;
+      }
+    }
+    ContentVersion content{};
+    ResidencyGeneration generation{};
+    if constexpr (Space::is_dense) {
+      content = world.meta(key).content_version;
+    } else {
+      if (const auto* meta = world.try_meta(key)) {
+        content = meta->content_version;
+      }
+      generation = world.residency_generation(key);
+    }
+    slice->deps.push_back({key, content, generation});
+  };
+  // The entry checks and the dense preamble read through the passability
+  // leaves; the hook forwards each resolved chunk key into the capture set.
+  // Installed only for the first call: the preamble never runs on resume,
+  // and resume-time reads are covered by the revalidation above. RAII, so
+  // the preamble's many early returns cannot leave it dangling.
+  using P2Record = std::remove_const_t<decltype(p2_record)>;
+  const detail::P2PassabilityHookGuard p2_guard{
+      slice != nullptr && !p2_resuming
+          ? +[](ChunkKey key, void* ctx) {
+              (*static_cast<const P2Record*>(ctx))(key);
+            }
+          : nullptr,
+      const_cast<void*>(static_cast<const void*>(&p2_record))};
+  if (!p2_resuming) {
+#endif
+    TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_count_);
+    scratch.clear();
+#ifdef TESS_P2_RESUMABLE
+  }
+#endif
   if (!contains<Shape>(request.start)) {
     return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
   }
@@ -88,6 +280,12 @@ template <typename World, typename Tag>
   // risk a false NoPath; it runs for dense worlds only. Sparse searches fall
   // straight through to the full A* below, which honors MissingChunkPolicy.
   // Dense codegen is unchanged (the guard is compiled away).
+  // The dense fast-path preamble is an atomic slice 0: it runs only on the
+  // first call and is skipped entirely on resume, because it can return a
+  // complete route and would otherwise re-answer a query already in flight.
+#ifdef TESS_P2_RESUMABLE
+  if (!p2_resuming)
+#endif
   if constexpr (Space::is_dense &&
                 std::is_same_v<typename ShapeTraits<Shape>::lattice_type,
                                lattice::Orthogonal>) {
@@ -628,23 +826,59 @@ template <typename World, typename Tag>
   const auto start = detail::tile_index<Shape>(request.start);
   const auto goal = detail::tile_index<Shape>(request.goal);
   const auto start_offset = space.offset(start);
-  scratch.g_[start_offset] = 0;
-  scratch.state_[start_offset] = open;
-  scratch.touch_node(start_offset);
-  TESS_DIAG_EVENT(path_touch_node);
-  TESS_DIAG_EVENT(path_heuristic);
   const auto model = Model{};
-  auto current_f = model.heuristic(world, request.start, request.goal);
-  scratch.open_.push_back(detail::PackedOpenNode::make(start, 0, current_f));
-  TESS_DIAG_EVENT(path_heap_push);
-
+  std::uint32_t current_f = 0;
   std::size_t expanded_nodes = 0;
+#ifdef TESS_P2_RESUMABLE
+  if (p2_resuming) {
+    // Resume: the frontier and node arrays are already in scratch; only the
+    // loop locals were lost across the pause.
+    current_f = slice->current_f;
+    expanded_nodes = slice->expanded_nodes;
+  } else {
+#endif
+    scratch.g_[start_offset] = 0;
+    scratch.state_[start_offset] = open;
+    scratch.touch_node(start_offset);
+    TESS_DIAG_EVENT(path_touch_node);
+    TESS_DIAG_EVENT(path_heuristic);
+    current_f = model.heuristic(world, request.start, request.goal);
+    scratch.open_.push_back(detail::PackedOpenNode::make(start, 0, current_f));
+    TESS_DIAG_EVENT(path_heap_push);
+#ifdef TESS_P2_RESUMABLE
+  }
+  if (slice != nullptr) {
+    slice->started = true;
+    slice->paused = false;
+    ++slice->slices;
+  }
+  std::size_t p2_this_slice = 0;
+#endif
   // Sparse worlds: set when the search skips a neighbor because its chunk is
   // not resident, so an exhausted search can return Indeterminate rather than
   // a NoPath it cannot justify. Never written for dense worlds (the guard that
   // sets it is compiled away), so dense still reports NoPath on exhaustion.
   [[maybe_unused]] bool crossed_missing = false;
+#ifdef TESS_P2_RESUMABLE
+  if (p2_resuming) {
+    crossed_missing = slice->crossed_missing;
+  }
+#endif
   while (!scratch.open_.empty() || !scratch.open_next_.empty()) {
+#ifdef TESS_P2_RESUMABLE
+    // The boundary sits at the top of the loop, before any frontier
+    // operation, so a pause performs no heap work and the pop order that
+    // decides the route is unaffected by where a slice ends.
+    if (slice != nullptr && slice->budget != 0 &&
+        p2_this_slice == slice->budget) {
+      slice->current_f = current_f;
+      slice->expanded_nodes = expanded_nodes;
+      slice->crossed_missing = crossed_missing;
+      slice->paused = true;
+      return PathResult{PathStatus::Indeterminate, 0, expanded_nodes,
+                        scratch.touched_count_, PathView{}};
+    }
+#endif
     if (scratch.open_.empty()) {
       // Saturating, mirroring the weighted core: a wrapped f would
       // mis-partition the two-bucket dial and let the goal close with a
@@ -667,6 +901,9 @@ template <typename World, typename Tag>
     }
     scratch.state_[current_offset] = closed;
     ++expanded_nodes;
+#ifdef TESS_P2_RESUMABLE
+    ++p2_this_slice;
+#endif

     if (current.index == goal) {
       auto step = current.index;
@@ -684,6 +921,63 @@ template <typename World, typename Tag>
     }

     const auto current_coord = detail::tile_coord<Shape>(current.index);
+#ifdef TESS_P2_RESUMABLE
+    if (slice != nullptr) {
+      // Interior expansions of this orthogonal +/-1-step core read only
+      // their own chunk; only a node on a chunk face (along an axis with
+      // more than one chunk) can read a neighboring chunk, so the full
+      // dependency enumeration is boundary-only. The model has no special
+      // transitions here (providers route to the weighted core), which the
+      // shortcut would otherwise have to forgo.
+      static_assert(!Model::has_special_transitions);
+      const auto p2_local = local_coord<Shape>(current_coord);
+      constexpr auto p2_chunk = ShapeTraits<Shape>::chunk;
+      const bool p2_crosses =
+          (ShapeTraits<Shape>::chunk_count_x > 1 &&
+           (p2_local.x == 0 || p2_local.x == p2_chunk.x - 1)) ||
+          (ShapeTraits<Shape>::chunk_count_y > 1 &&
+           (p2_local.y == 0 || p2_local.y == p2_chunk.y - 1)) ||
+          (ShapeTraits<Shape>::chunk_count_z > 1 &&
+           (p2_local.z == 0 || p2_local.z == p2_chunk.z - 1));
+      // Bounds-guarded face walk mirroring capture_field_product_dependencies
+      // rather than for_each_dependency_chunk: the raw enumeration sinks
+      // chunk keys of out-of-world candidate coordinates at map edges, whose
+      // unsigned-cast chunk coords produce out-of-range keys that meta()
+      // would index unchecked. Face steps cross at most one chunk face, so
+      // only the faces this node actually sits on are captured.
+      const auto p2_center = chunk_coord<Shape>(current_coord);
+      p2_record(chunk_key<Shape>(p2_center));
+      if (p2_crosses) {
+        if (p2_local.x == 0 && p2_center.x > 0) {
+          p2_record(chunk_key<Shape>(
+              ChunkCoord3{p2_center.x - 1, p2_center.y, p2_center.z}));
+        }
+        if (p2_local.x == p2_chunk.x - 1 &&
+            p2_center.x + 1 < ShapeTraits<Shape>::chunk_count_x) {
+          p2_record(chunk_key<Shape>(
+              ChunkCoord3{p2_center.x + 1, p2_center.y, p2_center.z}));
+        }
+        if (p2_local.y == 0 && p2_center.y > 0) {
+          p2_record(chunk_key<Shape>(
+              ChunkCoord3{p2_center.x, p2_center.y - 1, p2_center.z}));
+        }
+        if (p2_local.y == p2_chunk.y - 1 &&
+            p2_center.y + 1 < ShapeTraits<Shape>::chunk_count_y) {
+          p2_record(chunk_key<Shape>(
+              ChunkCoord3{p2_center.x, p2_center.y + 1, p2_center.z}));
+        }
+        if (p2_local.z == 0 && p2_center.z > 0) {
+          p2_record(chunk_key<Shape>(
+              ChunkCoord3{p2_center.x, p2_center.y, p2_center.z - 1}));
+        }
+        if (p2_local.z == p2_chunk.z - 1 &&
+            p2_center.z + 1 < ShapeTraits<Shape>::chunk_count_z) {
+          p2_record(chunk_key<Shape>(
+              ChunkCoord3{p2_center.x, p2_center.y, p2_center.z + 1}));
+        }
+      }
+    }
+#endif
     model.for_each_forward(
         world, current_coord, current.index, [&](auto probe) {
           TESS_DIAG_EVENT(path_neighbor_candidate);
diff --git a/include/tess/path/path.h b/include/tess/path/path.h
index 80228c80..276658b8 100644
--- a/include/tess/path/path.h
+++ b/include/tess/path/path.h
@@ -714,6 +714,14 @@ static_assert(sizeof(PackedOpenNode) == 2 * sizeof(std::uint64_t));
 ///
 /// Instances are caller-owned and require external synchronization. Reserving
 /// for the search space avoids allocation once warm.
+#ifdef TESS_P2_RESUMABLE
+// P2 screen scaffolding, never merged. Defined with the search core in
+// detail/astar.h; carried on the scratch rather than passed as a parameter,
+// because an extra trailing pointer argument is indistinguishable from the
+// transition-provider overload during deduction.
+struct P2Slice;
+#endif
+
 class PathScratch {
  public:
   struct OpenNode {
@@ -722,6 +730,15 @@ class PathScratch {
     std::uint32_t f = 0;
   };

+#ifdef TESS_P2_RESUMABLE
+  // P2 screen scaffolding, never merged. A caller that wants a sliced search
+  // points this at its own P2Slice; leaving it null is exactly today's
+  // behaviour. Carried here rather than as a trailing parameter because an
+  // extra pointer argument is indistinguishable from the transition-provider
+  // overload during template deduction.
+  P2Slice* p2_slice_ = nullptr;
+#endif
+
   void reserve_nodes(std::size_t node_count) {
     open_.reserve(node_count);
     open_next_.reserve(node_count);
@@ -1310,6 +1327,41 @@ template <typename Shape>
   return coord<Shape>(TileKey<Shape>{static_cast<Storage>(index)});
 }

+#ifdef TESS_P2_RESUMABLE
+// P2 screen scaffolding, never merged. The dense fast-path preamble reads
+// world data exclusively through the two passability leaves below, so a
+// sliced query captures the preamble's chunk dependencies by installing this
+// thread-local hook for the duration of the entry checks and preamble. A
+// plain function pointer plus context pointer rather than std::function:
+// installation must not allocate, and the leaves are noexcept.
+inline thread_local void (*p2_passability_hook)(ChunkKey, void*) = nullptr;
+inline thread_local void* p2_passability_hook_ctx = nullptr;
+
+// RAII: the preamble has many early returns, several of which deliver a
+// complete route, so clearing the hook cannot rely on reaching a statement.
+// A hook left installed would dangle into a dead stack frame on the next
+// passability read anywhere on the thread.
+class P2PassabilityHookGuard {
+ public:
+  P2PassabilityHookGuard(void (*hook)(ChunkKey, void*), void* ctx) noexcept {
+    p2_passability_hook = hook;
+    p2_passability_hook_ctx = ctx;
+  }
+  ~P2PassabilityHookGuard() {
+    p2_passability_hook = nullptr;
+    p2_passability_hook_ctx = nullptr;
+  }
+  P2PassabilityHookGuard(const P2PassabilityHookGuard&) = delete;
+  P2PassabilityHookGuard& operator=(const P2PassabilityHookGuard&) = delete;
+};
+
+inline void p2_report_chunk_read(ChunkKey key) noexcept {
+  if (p2_passability_hook != nullptr) {
+    p2_passability_hook(key, p2_passability_hook_ctx);
+  }
+}
+#endif
+
 // The passability/cost leaves take a movement class OR a raw passable tag
 // (normalized through movement_class_of, so every legacy <World, Tag> call
 // site compiles unchanged and the identity class keeps codegen byte-identical
@@ -1322,6 +1374,12 @@ template <typename World, typename ClassOrTag>
   if (!resolved.has_value()) {
     return false;
   }
+#ifdef TESS_P2_RESUMABLE
+  // Reported before the residency probe: on sparse worlds "not resident"
+  // is itself a conclusion this read contributes, so the chunk is a
+  // dependency whether or not a page backs it.
+  p2_report_chunk_read(resolved->chunk_key);
+#endif
   if constexpr (std::is_same_v<typename World::residency_type,
                                SparseResident>) {
     const auto* page = world.try_chunk(resolved->chunk_key);
@@ -1339,6 +1397,10 @@ template <typename World, typename ClassOrTag>
   using Shape = typename World::shape_type;
   using Storage = typename ShapeTraits<Shape>::TileKeyStorage;
   const auto key = TileKey<Shape>{static_cast<Storage>(index)};
+#ifdef TESS_P2_RESUMABLE
+  // Before the residency probe, matching is_passable above.
+  p2_report_chunk_read(chunk_key<Shape>(key));
+#endif
   if constexpr (std::is_same_v<typename World::residency_type,
                                SparseResident>) {
     // A non-resident chunk carries no data, so it reads as impassable. This
```

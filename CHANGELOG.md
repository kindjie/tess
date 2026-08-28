# Changelog

Notable, release-facing changes to `tess`. All `0.x` releases are
pre-stable: minor versions may change public APIs and data layouts
without compatibility shims. The format loosely follows
[Keep a Changelog](https://keepachangelog.com/); design-level decisions
and their rationale are recorded separately in
[`docs/decisions/CHANGELOG.md`](docs/decisions/CHANGELOG.md).

## [Unreleased]

## [1.0.0-rc.1] - 2026-08-28

### Added

- `PathAgentFrameStats` now reports `expanded_nodes`, the total search nodes
  expanded by the completed results a processing or replan-drain call applied.
  This gives callers a deterministic work meter: planning can be budgeted by
  search effort per tick where a wall-clock budget would break replay.
- `tess::movement::OverlayCost<Base, Overlay>` prices a base cost with an
  additive overlay -- terrain plus congestion, tolls, or any other
  surcharge -- saturating at the 32-bit maximum. It is zero if and only
  if its base is zero, so an overlay can never make impassable ground
  enterable, matching the rule the transition model already applies
  where a provider's cost meets a movement class's entry cost. The
  operands are not interchangeable: the overlay's zero means "no
  surcharge", which is the one place in this vocabulary where zero is
  not the impassable sentinel.
- `PathAgentReplanQueue::contains(index)` reports whether an agent is
  currently waiting in the queue. Membership is what makes `request`
  idempotent, and exposing it lets a caller skip work that would only
  build a request certain to be refused.
  `tess::experimental::request_replans_for_route_crossings` now uses it
  to skip agents already pending: under a bounded planning budget the
  backlog persists across repricings, so this is the difference between
  rescanning the whole backlog every time and scanning only what is new.
  Queue contents, ordering, and the returned count are unchanged.
- Added `tess::experimental::request_replans_for_route_crossings`
  (`tess/experimental/path_agent_replan_selection.h`): asks the replan
  queue for exactly the agents whose remaining retained route crosses
  a caller-nominated tile, the scoped-replanning discipline that keeps
  periodic cost-field edits (congestion pricing, tolls, seasonal
  terrain) from replanning every agent. Experimental tier; contract
  pinned by direct fixtures.
- Release-mode CI now fails unless the release records are assembled:
  `assemble_changelog.py --require-released VERSION` rejects pending
  fragments in any stream, a missing released section for VERSION, and
  duplicate sections for one version, and `release-evidence` runs it
  with the dispatch's expected version. Fragment-syntax validation alone
  had reported success while 24 fragments sat unassembled beside an
  already-dated RC1 heading. Rerunning `--release` for a version that
  already has a section is also refused regardless of date, so a redate
  must fold back rather than append a second section.
- A tower demo: a six-floor tess world where floors are separated by
  solid slabs and stairwell columns are the only tiles connecting them,
  so crossing floors follows one route through three dimensions rather
  than switching between stacked maps. Closing a stairwell prices it
  rather than sealing it, so routes divert while anyone on the stairs
  can still walk out. Published beside the other browser demos, with an
  isometric 2D-canvas view and touch controls sized for a phone.

### Changed

- The congestion-pricing example keeps terrain and the congestion
  surcharge in separate fields, summed by
  `tess::movement::OverlayCost`, instead of writing prices into the
  field the movement class reads. Disarming clears the surcharge and
  leaves terrain untouched; the previous shape restored unit costs
  everywhere, which is correct only on uniformly unit terrain and
  destroys a real terrain map. Settle behaviour is unchanged: on unit
  terrain the summed cost is identical tile for tile, and the example
  reports the same arrivals and scoped replans as before.
- The pre-commit hook now runs the inventory tests when a change stages
  `examples/CMakeLists.txt`, `tests/CMakeLists.txt`, or the CI workflow.
  Those tests assert exact example and smoke counts against the CMake
  declarations, and forgetting to run them turned a one-line count into
  a red pull request twice. Other commits pay nothing: the check is a
  no-op unless a triggering file is staged.
- The path-agents example reports `re-search failed` rather than
  `repath failed`, matching the reserved vocabulary where *search* names
  graph work.
- The 1.0.0-rc.1 compatibility consumer now instantiates
  `tess::movement::OverlayCost` and checks that it adds, saturates, and
  absorbs a zero base, both on the expression and through a weighted
  search. The snapshot previously recorded only the bare name, which
  would have let an incompatible change to a newly stable type pass.
- `tess::experimental::request_replans_for_route_crossings` now begins its
  scan at the agent's next step rather than at the tile it occupies.
  Movement charges a tile's cost on entering it, so a price rise on the
  occupied tile cannot change the cost of the route that remains, and
  scanning it queued replans that could not improve anything. A route
  that revisits that tile later is still detected, at the later index.
- The weekly long-seed property sweeps and advisory coverage jobs moved
  from `ci.yml` into a `scheduled-sweeps.yml` reusable workflow that
  `ci.yml` calls, bringing `ci.yml` back under the repository's
  24,000-token per-file limit with room to edit it again.
- The tower demo's rooms are now on the route rather than decorative.
  Two of its four stairwells serve each floor, alternating as you climb,
  so an agent arriving on a floor must cross it to reach the next way
  up; previously every stairwell was passable at every level and the
  cheapest route ran straight up one shaft without ever stepping onto an
  intermediate floor. The endpoint floors gained the same structure
  outside the endpoint block itself, and walls are drawn with height so
  the room outlines are legible.

### Fixed

- The CI quality job allows 75 minutes rather than 45. The full-tree
  clang-tidy sweep runs about 28 minutes with a warm compiler cache, but
  a change to a widely included header invalidates most of that cache in
  a header-only library. Two consecutive default-branch runs were
  cancelled at the old limit after one such change, and because a
  cancelled job saves no cache, every later run began equally cold — a
  deadlock that could not clear itself.
- The compiled congestion example no longer calls global pathing-dirty
  the "~500x mistake". The retained measurement is about 84 ms against
  1.6 ms per tick, roughly 53x; the guide and the evidence summary were
  corrected earlier and the example comment was missed.
- Fix the congestion and tower documentation links so unreleased demos resolve
  inside the development documentation tree and automatically follow their
  containing version after release.
- A saturated `PathAgentState::path_index` no longer restarts a consumed
  route. `path_index` is a public field with no enforced range, and the
  five stable advance paths plus the experimental replan-selection
  helper computed `path_index + 1` before their bound check; at
  `SIZE_MAX` that wraps to zero, which compares as in range. An advance
  would step the agent onto `route[0]`, and the helper would rescan a
  route its own comment calls fully consumed.
- Corrected pre-1.0 audit findings in maintained documentation: the
  spatial-coordination page now shows the two-field `OverlayCost`
  congestion shape instead of the single-field one the guide warns
  against, the topology reference gives `OverlayCost`'s real namespace,
  header, and stable tier, and the compatibility page names archive
  format v2 rather than v1.
- Attached measurement conditions to the headline performance numbers,
  which are now remeasured at a named commit, and recorded that the
  span-query figures come from a run whose conditions were never
  recorded.
- Replaced durable "pre-1.0" wording in a shipped header and two
  architecture pages, and added a check that rejects its return once a
  1.x release exists.
- The two remaining provisional 2x entries in
  `bench/thresholds/path.json` are recalibrated to that manifest's own
  6x bootstrap convention over a fresh local arm64 median. One of them
  had been cancelling default-branch runs. Its 2x ceiling of 101,000 ns
  sat *below* the gate's own recorded CI medians, which span 68,331 to
  101,147 ns, so the gate could fail on work counters identical to a
  passing run. No benchmark regressed: the local median is 53,316 ns at
  0.32% variation, essentially unchanged from the 50,096 ns the original
  ceiling was derived from. The file's seven explicitly uncalibrated
  `BOOTSTRAP 4x` entries are unaffected and stay provisional.
- Restored canonical documentation at stable root URLs, kept `/latest/`
  compatibility redirects and resources, repaired sitemap and social-card
  URLs, and hardened Pages checks against the final uploaded artifact.
  Publication now uses a lossless FIFO turn and validates the complete tree
  before its single storage push.
- Documentation publication now applies the root `robots.txt` policy on
  the established-root path as well as the bootstrap path. The previous
  fix only ran where the root was created, so on the live tree — which
  has a publication manifest — the legacy `Disallow: /dev/` survived and
  the artifact check correctly rejected it, failing every `main`
  deployment. A root file the manifest does not own is never rewritten;
  publication fails instead.
- Documentation links now stay inside their own version tree. Absolute
  same-origin links in `docs/` published into every version, so a reader
  on the development tree who followed one silently landed on released
  content — and a link to a page that existed only in the newer tree
  returned 404 until a release caught up. A regression test rejects the
  pattern rather than the instances.
- The docs publication check refuses a root `robots.txt` that disallows
  a path. A crawler forbidden to fetch a page never reads that page's
  `noindex`, so disallowing a version tree pins whatever is already
  indexed instead of retiring it, which is the opposite of the intent.
- The published root `robots.txt` is now written by the publication tool
  rather than inherited from the released tree the root is derived from.
  The live file still carried `Disallow: /dev/` from before the version
  trees switched to `noindex, follow`, which kept crawlers from reading
  the very noindex meant to retire them.
- The pathfinding guide no longer restates A* timings that the
  performance page had already corrected; it defers to that page, which
  records the measurement conditions alongside the figures.
- The strategy-comparison timing table, whose original run's commit and
  toolchain were never recorded, is regenerated from commit `d653d813`
  on two platforms — Apple M3 Max and Steam Deck — with conditions and
  raw outputs retained. The unprovenanced ratios reproduced within
  0.2, and the ordering held on both architectures.

### Documentation

- Documented the measured anonymous-goal dispatch recipe in the spatial
  coordination architecture notes: spend exact assignment once at
  dispatch (greedy dispatch settled 47% slower on the same pools);
  continuous post-dispatch reassignment measured ~2% and is not worth
  library authority.
- Documented the validated dynamic congestion pricing caller recipe
  (bounded demand-driven cost-field pricing through versioned edits) and
  its measured boundary in the spatial coordination architecture notes.
- Documented the caller-side refresh obligation on the
  `cached_astar_path` declarations: direct adopters must run
  `UnitRouteCache::refresh_if_world_changed` after world edits, or a
  cache hit can serve the pre-edit route (found by the RC-1 downstream
  evaluation).
- The documentation site now publishes one tree per version. Released
  documentation lives at `/latest/` with a per-release archive at
  `/<major>.<minor>/`, `main` publishes to `/dev/`, and a version selector
  moves between them. Every documented link now points at `/latest/`, and
  the site root redirects there.
- Added the congestion-pricing decision guide
  (`docs/guide/congestion.md`) with a compile-checked copyable
  implementation (`examples/congestion_pricing.cc`) and a browser
  laboratory (`/demo/congestion/`) running every screened pricing
  policy live over the colony simulation with a price-heat overlay;
  the colony demo itself returns to a clean tutorial. The spatial
  coordination recipe now prescribes scoped replanning.
- Comments and maintained notes that described graph work as a "re-path"
  or a "path attempt" now say "re-search", matching the vocabulary the
  terminology guide reserves: *search* for the work, *path* for a
  returned coordinate sequence, *route* for retained planning state. The
  sweep covers the public headers, maintained architecture notes,
  examples, tests, and benchmarks; historical records keep their
  original wording, and the stable `repath*` field names are unchanged.
  Comment-only; no API, behaviour, or identifier changed.

## [0.13.0] - 2026-08-20

### Added

- Added lossless `Coord2` use across canonical world-space APIs and dense-world
  `fill_field<Tag>(value)` setup, then simplified the README, tutorials, native
  examples, and WebAssembly demo models around those conveniences.
- The budgeted-progress suite gains its colony-derived cells:
  incremental region-graph updates over four toggling dirty chunks per
  event, and the queued one-op-per-chunk update path through
  AutoExecTask planning, execution, and dirty merge — both reusing the
  colony harness's deterministic map, cost, and churn machinery, with
  pre-timing equivalence against a fresh topology rebuild and
  byte-exact toggle restoration.
- The budgeted-progress suite gains its section 11.2 counter pass: a
  diagnostics-enabled twin binary collects detailed deterministic path
  counters over the identical demand traces (measured windows only,
  never a source of published time), artifacts carry a pass marker and
  counters block, exact zero-tolerance work identities run inside both
  binaries, and tools/compare_budgeted_passes.py enforces the
  regime-classified cross-pass tolerances with regime divergence
  reported as a finding rather than a failure.
- tools/summarize_budgeted_curves.py regenerates the budgeted-progress
  capacity and completion curves solely from artifacts (the stage-4
  acceptance contract): section 12 summary rows for isolated cells,
  arrival and mixed matrices, and capacity bands, with matrix holes
  named in coverage notes and CSV files beside the Markdown.
- The budgeted-progress suite gains the stage-3 mixed colony: the
  production colony stack (schedule, queued churn, incremental
  topology, weighted path agents, render deltas) driven from a paced
  60 FPS frame loop with per-frame budgets, proven equivalent to the
  colony harness's reference run before any budgeted cell executes,
  with ping-pong navigation demand, the canonical 32-tick interactive
  deadline, 64-tick settlement, closed-loop stability verdicts, and
  both section 7.3 views (identical by construction in this stack and
  documented as such).
- The budgeted-progress suite gains paced arrival cells: frames wait
  for their 60 FPS edges on the monotonic clock, so artifacts carry
  measured completions-per-wall-second, the measured wall span, and
  the frame-start-lag distribution — the design's adopter-facing rate
  claim, published from paced cells exclusively (unpaced artifacts
  must omit wall rates, enforced by the fail-closed validator).
- The budgeted-progress mixed colony takes a `--mixed-views` selector
  so campaigns run one of the two identical-by-construction section
  7.3 views and treat the other as analytically equal, halving paced
  device time; the default still runs both for smoke plumbing
  coverage.
- Add cross-cut endpoint parking and optional congestion-triggered equal-cost
  route spreading to the browser colony, including stable-topology retries and
  one additional bounded seed for directly observed wide wall-tip merges,
  without weakening planning budgets or terminal outcome classification.
- Show the browser colony's pending route plans, last-tick moves, and
  movement waits so bounded planning backlog is distinguishable from routed
  congestion without changing the simulation's movement policy.
- Add a native scenario mode to the colony example for reproducible open,
  wall-tip, two-gate, four-gate, and guarded goal-wall campaigns without
  adding a benchmark framework or browser dependency.
- Added `World::mark_content_changed` to invalidate version-keyed derived
  state without creating dirty-mask work, with identical dense and sparse
  semantics and stale dirty-observation refusal.
- Add a public bounded path-agent replan callback that composes custom Tess
  planners with retained-route FIFO and lifecycle handling.
- Added a preallocated registered dirty-bit maintenance scheduler with
  allocation-free warm-path scheduling, constant-time queued coalescing
  membership, generation-aware world-backed correctness tests, and sparse,
  dense, mixed, budgeted, flush, and scaling benchmarks.
- Added an experimental external chunk-maintenance adapter with fixed dense
  and sparse slots, generation-stamped derived products, explicit quiescent
  residency transitions, version-drift repair, retained retry debt, budgeted
  retry, and a self-checking installed-package example.
- Pull requests now compile the library against libc++
  (`-stdlib=libc++`, compile-only, mirroring the GCC portability cell).
  libc++ was previously covered only by the macOS jobs, which are
  main-only, so a libc++-specific failure could reach main before anyone
  saw it.
- The budgeted-progress mixed-colony benchmark gains the design's
  1024x1024 capacity cell (`--mixed-world 1024`): the mixed machinery is
  parameterized by world shape over the same 64x64 logical map (scale
  16), the population ladder extends through 1000 and 2000 (stride-2
  seating capacity there is 4120, checked by an up-front preflight and a
  smoke ctest at the ladder maximum), world extent joins the scenario
  identity, filenames, and trace parameters, and the per-tier
  equivalence proof runs at each requested world.
- The budgeted-progress mixed-colony benchmark gains a movement-tier
  axis (`--mixed-tiers baseline,pibt`): the PIBT tier runs the colony
  harness's new `ColonyConfig::movement_tier` dispatch with the
  route-attachment ranking, each tier is a separate `scenario_id`
  cohort with a `movement_tier` field, artifacts additionally record a
  realized-churn hash (applied edits are occupancy- and therefore
  tier-dependent), and the cross-pass comparator now pairs on the full
  cell identity and rejects duplicate identities instead of silently
  shadowing them.
- `tess::RouteAttachmentRanking`: a production PIBT ranking oracle that
  scores candidates by local attachment to each agent's retained A* route
  (bounded attachment radius plus remaining route length, with steer-back
  for detached candidates and a Manhattan fallback for routeless agents).
  Terrain-blind distance heuristics park agents at wall-adjacent local
  minima that yields cannot fix; the bounded route attachment matches an
  exact whole-map distance oracle in the mixed-colony stranding
  experiment at a fraction of the cost.
- Release archives now provide deterministic portable header bundles with the
  concrete version header, license, source identity, and checksums. Consumers
  can extract one bundle and compile against its `include` directory without
  CMake or a package manager; exact-SHA release CI directly exercises the same
  retained tar and zip bytes with Clang, GCC, and MSVC.
- Opt-in portal-first serving for single-goal weighted replans:
  `PathRuntimeCachePolicy::weighted_replan_strategy =
  WeightedReplanStrategy::PortalFirst` routes eligible singletons (dense
  worlds, default adjacent transitions, explicit movement classes)
  through a chunk-portal route stitched via the runtime's segment cache
  before falling back to exact A*. Accepted routes are legal and
  verified but may exceed the optimal cost, bounded by a premium cap
  (default 4/3 of the Manhattan lower bound, so at most 4/3 of optimal);
  every other outcome — no candidate, a failed segment, a cap rejection,
  or an ineligible request — is served by exact A* with byte-identical
  results, and per-outcome statistics are reported. The default strategy
  is unchanged. A new cache-aware builder,
  `build_weighted_chunk_portal_route_product_cached`, exposes the same
  composition to direct callers. On the goal-churn benchmark map the
  repeated-churn tick drops from ~18.8 ms to ~24 us on the calibration
  machine, and genuinely fresh cross-map goals drop from ~2.0 ms to
  ~67 us under a 2/1 cap (the same goals all reject under the default
  4/3 cap on this map — the cap dials the quality/latency trade);
  the forced all-rejected case costs about 2% over exact, and the
  no-portal-route worst case (candidates select, stitching fails, the
  exact NoPath flood follows) about 2x — every shape carries its own
  cell with premise-and-outcome asserts.
- Added an experimental fixed-registration maintenance contract with opaque
  owner-and-generation handles, checked stale operations, explicit capacity
  and drain outcomes, and unconditional lifecycle diagnostics. Callback
  exceptions continue to propagate verbatim.
- Add retained-route path-overlay collection for scoped submissions and
  queue-produced routes whose runtime tickets are stale or value-zero.
- Opt-in scoped staleness for the unit route cache:
  `PathRuntimeCachePolicy::unit_route_staleness =
  UnitRouteStaleness::ScopedFeasible` keeps cached routes whose chunk
  footprint an edit did not touch, instead of dropping the whole cache on
  any world change. Surviving routes are legal with a truthful cost and
  were optimal when stored; an edit elsewhere that opens a shortcut can
  leave a served route suboptimal until it is retired (blocking-only edit
  sequences preserve optimality). Applies to unit-cost movement without
  special transitions on dense worlds; other models and sparse worlds keep
  today's exact whole-cache behavior. The default is unchanged. Direct
  `UnitRouteCache` users get the same machinery via `set_staleness` and
  `refresh_if_world_changed`. On the profiled steady off-route edit shape,
  the world-edit agent tick drops from ~415 us to ~130 us on the
  calibration machine; two new benchmark cells pin the survival steady
  state and the forced retire-every-tick worst case.
- Add opt-in deterministic seeded tie-breaking to exact weighted A* searches
  and bounded weighted replan queues, preserving canonical ordering for a zero
  seed and optimal cost for every seed.
- Added opt-in `tess/io.h` stream insertion for coordinates, extents, path
  statuses, and borrowed paths without adding iostream dependencies to core
  headers.
- Added a deterministic 1024×512 WebAssembly Traffic Lab with 1,024 agents,
  four congestion scenarios, bounded planning, cached terrain rendering, and
  update, planning, render, frame, wait, blockage, progress, and opt-in tail-
  latency diagnostics with separate timing and counter passes.
- Colony tutorial wall strokes now draw or erase according to their initial
  cell, with C++ retaining authority over every desired-state edit.
- Added explicit Traffic Lab native self-check modes for fast scenario smoke,
  512/1,600-tick guided crowd replay, and exhaustive route validation.
- Add a live WebAssembly diagnostics demo that renders the reference Dear
  ImGui panels over real path and queued workloads, with accessible mirrored
  controls and browser-verified readiness.

### Changed

- Establish canonical pre-1.0 terminology across the public API and docs:
  explicit metadata types, derived chunk activity, archive format v2,
  ownership-accurate cache and operation names, explicit weighted movement
  classes, truthful topology-version aggregates, and conservative
  sparse-boundary defaults. Path agents now separate lifecycle phase from an
  optional last search result instead of using `NoPath` as an unsearched
  sentinel. Path products now distinguish `NotComputed`, heuristic
  `NoCandidate`, and authoritative `NoPath` outcomes; two-call sparse fields
  preserve indeterminate builds through later path reads.
- Changelog entries are now written as per-change fragments under
  `changelog.d/` and `docs/decisions/changelog.d/`, assembled into the
  maintained changelogs at release by `tools/assemble_changelog.py`.
  Concurrent branches no longer conflict on a shared changelog file.
- Reorganize the colony WebAssembly demo into tutorial-oriented model and
  platform seams, add presentation-only fixed-tick movement interpolation,
  and recover rejected render-delta streams through a full baseline.
- `DeltaCollector` is now move-only. A copy silently duplicated all five
  published and pending buffer pairs, and left two collectors each
  believing they were the sole owner clearing the dirty bits they
  collected. Collection *consumes* those bits, so a second collector over
  the same world observes nothing and publishes an empty frame that still
  advances its own version — a consumer on that chain misses every
  invalidation with no signal. Nothing in the tree copied one, so this
  breaks no existing code.
- It stays movable, because factories return a collector by value.
  Declaring the copy operations — even as deleted — suppresses the
  implicit move operations, so those are defaulted explicitly rather than
  left silently absent; a test pins both halves. Moving still invalidates
  a live `DeltaFrame`, whose spans then point into buffers the destination
  owns, and the lifetime contract says so.
- A moved-from collector now behaves as if `clear()` had been called on
  it: its next publish is forced truncated, so a consumer on its chain
  resyncs rather than accepting an empty frame as an applicable no-op.
  Deleting the copy operations alone would have *relocated* the hazard
  rather than removed it — the defaulted move transfers the buffers but
  copies the protocol scalars, so a moved-from collector kept its version
  and baseline flag. Review reproduced the consequence: re-reserve the
  source, let the destination collect first, and the source publishes an
  applicable, untruncated, empty frame on a chain that still looks
  continuous.
- The poison lives in the move operations of a one-member sentinel rather
  than in a hand-written `DeltaCollector` move, so both enclosing moves
  stay `= default` and every member — including any added later —
  participates memberwise as usual. Hand-writing the enclosing move would
  have silently dropped a future member, which is the same class of silent
  failure as the bug.
- Bounds, stated so the fix is not oversold: this closes the moved-from
  chain-continuity hole only. Two live collectors clearing one world's
  dirty bits remains the sole-clearing-owner contract, and no type-level
  check can enforce it.
- Reuse a moved-from collector by assigning a fresh one to it, or by
  `clear()` and a baseline collection. `reserve()` looks like a reset and
  is not one.
- Three types that appeared in public signatures while living in
  `tess::detail` are now public, because `docs/style.md` says `detail`
  carries no source-compatibility guarantee and each of these was
  something a consumer had to name:
  - `UInt128` — `TileKey::value` is spelled with it for any shape needing
    more than 64 tile-key bits. `core/uint128.h` moves from the
    implementation header set to the public one accordingly. Shapes that
    fit in 64 bits keep a plain `std::uint64_t`, so most consumers never
    encounter it.
  - `PhaseDirtyPartition` — `PlannedPhaseExecutionScratch::dirty_partitions()`
    returns a span of it, so reading per-operation dirty records required
    naming a `detail` type.
  - `PibtRanking` — it constrains public PIBT entry points, so a caller
    whose ranking callable was rejected could not name the concept
    deciding it.
- `UInt128`'s implicit constructor from a signed integer is templated over
  `std::signed_integral` rather than taking a plain `int`. An `int`
  parameter accepted any wider signed value through a silent narrowing
  conversion, so `UInt128 v = std::int64_t{1} << 32` was **zero**. That
  was a latent defect while the type was internal; promoting it would have
  made it a public one.
- `UInt128`'s operator set stays deliberately partial and is now pinned by
  `tess_uint128_surface_test`, which asserts both that the supported
  operations compile and that addition, division, modulo, increment and
  implicit narrowing do not. It carries packed key bits; it is not a
  general 128-bit integer, and a comment alone would not have stopped the
  surface growing one convenience operator at a time.
- `integration-policy.md` gains a section on
  `include/tess/experimental/`: names there may change or be removed in
  any release and are excluded from a future 1.0 promise. Within it,
  `FifoScheduler` and `CoalescingScheduler` are the supported spellings,
  while the template they alias, `detail::QueuedScheduler`, deliberately
  is not — that indirection exists so the alias can be repointed without
  breaking callers. Promoting the template would have frozen an
  implementation detail of the layer explicitly labelled unstable.
- `PibtFrame` stays in `detail` and no longer appears in any supported
  declaration: its one public appearance was `PibtPriorities::frames`, and
  that member is now private. The type is still defined in the installed
  header, as every `detail` name in a header-only library is; what changed is
  that naming it is no longer something the promise requires. Promoting it
  would have frozen an implementation struct instead.
- The docs site now serves Mermaid from its own origin: a pinned,
  SHA-256-verified 11.16.1 runtime is fetched at build time
  (`tools/fetch_mermaid.py`) and loaded ahead of the theme bundle, so
  diagram pages no longer depend on unpkg.com or float within the Mermaid
  major. CI validates every ` ```mermaid ` fence against that exact runtime
  (`tools/check_mermaid.py`) — parse failures previously shipped silently as
  raw diagram source. Pages also carry Open Graph/Twitter card metadata
  backed by the existing social-preview image, which is now published with
  the site.
- `JointMoveScratch`'s eleven round buffers and `PibtPriorities`'s decision
  order and inheritance stack are private. Every one of them was a public
  member under a comment calling it an implementation detail, which is not a
  boundary: the comment said nothing about what the pass would do with a
  buffer a consumer had sized, read, or overwritten, and 1.0 would have
  frozen the layout regardless. `reserve` remains the whole surface of the
  scratch; `elapsed` and `reserve` remain the whole surface of the
  priorities, since adaptive priorities are the caller's knob. The buffers
  are no longer reachable through either type; like every internal in a
  header-only library they stay spellable through `tess::detail`, which
  `docs/style.md` excludes from source-compatibility, so what moved is what
  the promise covers rather than what a determined consumer can name.
- Source-breaking for code that named a buffer, aggregate- or designated-
  initialized either type, decomposed one in a structured binding, or relied
  on `PibtPriorities` being standard-layout — it mixes a public `elapsed`
  with a private member now, so `offsetof` and the trait no longer apply.
  That last one only changes anything on a standard library whose
  `std::vector` is itself standard-layout: under MSVC's STL neither type was
  standard-layout to begin with, and `JointMoveScratch` keeps whatever it
  had, since all of its members are private. No in-repo consumer, example,
  or benchmark did any of those. Default construction, `{}` value
  initialization, copy and move all still compile.
- `PibtPriorities::frames` was also the last public member typed with a
  `detail` struct. Privatizing it closes that leak without promoting
  `PibtFrame`, which would have frozen an implementation layout — the
  outstanding half of the same finding.
- `tess_joint_movement_test` and `tess_pibt_movement_test` pin all of this,
  each negative probe paired with a control type carrying the same member
  name in public so a mistyped name cannot produce an assertion no type
  could fail.
- The docs-site top navigation now fits without clipping: thirteen top-level
  tabs are consolidated into eight (Installation and Examples join Getting
  started; Integration policy, Roadmap, Support, and For agents join a new
  Project section), and `navigation.indexes` attaches the Decision guide and
  Concepts overview pages to their section titles. No page URLs changed.
- 106 public functions returning a status or result type are now
  `[[nodiscard]]`, closing the gap the audit named: 529 sites already had
  the attribute, so these were the exceptions rather than the rule.
  Because the library is exception-free, the returned value is the only
  failure channel — `load_world_archive(world, bytes);` as a bare
  statement silently ignored `Corrupt`. Source-breaking for callers that
  discard these values under `-Werror`, which is why it lands before 1.0
  rather than after.
- `build_region_graph` is deliberately excluded and now documents why. Its
  status is invariantly `Built`: the dense branch iterates keys
  `0..chunk_count` so `InvalidChunk` cannot arise and `MissingChunk` does
  not exist under `AlwaysResident`, and the sparse branch builds from
  `resident_chunk_keys()`, which are in-world and resident by
  construction. `update_region_graph` does have a reachable failure —
  `InvalidChunk` for an out-of-range dirty chunk — and is marked.
- `update_region_graph` discards the per-chunk build status in both of its
  incremental branches, and both are now written as deliberate with the
  argument that makes each sound. They differ: the dense branch relies on
  `MissingChunk` not existing under `AlwaysResident`, the sparse branch on
  the residency-generation check having already diverted every eviction to
  a full rebuild.
- `build_region_graph`, which cannot fail, returns `RegionGraphBuildResult`;
  `update_region_graph`, which can reject an invalid dirty chunk, returns the
  status-bearing `TopologyBuildResult`.
- `IntentPayloadView` gains `holds<T>()` and `bound()`, and `as<T>()` now
  asserts `holds<T>()` instead of answering a wrong-type query with an empty
  span. That span was also what a correctly-typed empty batch returned and
  what an operation carrying no payload returned, so a caller that asked for
  the wrong type processed nothing every frame with no signal to distinguish
  it from a quiet frame. The typed `OperationBatch` entry points pair each
  `OperationKind` with the payload type it names, so a consumer dispatching
  on `kind` over operations from those entry points has the type fixed for
  it and a mismatch is a caller bug rather than a condition to branch on;
  nothing enforces that pairing on a hand-built operation. `holds<T>()`
  answers the question where it is genuinely open, and `bound()` separates
  "no payload" from "empty batch".
- Source-compatible, behaviour-breaking in one direction only: a build with
  assertions enabled now aborts where it previously returned an empty span.
  With assertions compiled out the empty-span fallback remains, so a release
  consumer keeps the old behaviour rather than reading one type's bytes as
  another's. Both halves are pinned — the abort by death tests, the fallback
  by the NDEBUG cell that compiles the same source.
- `holds<T>()` documents what its identity token actually guarantees and no
  more: uniqueness per type within one binary image. The answer for a
  payload built in another image is unspecified and toolchain-dependent, so
  payloads should not cross a shared-library boundary — the same caveat
  `docs/integration-policy.md` already records for the token. `bound()`
  documents what it cannot do: this is a non-owning view, and a dangling
  `data` pointer is indistinguishable from a live one.
- `ResumableWorkQueue`'s five mutators keep their `bool` and gain
  documentation of what it means: whether the call changed the state. The
  three causes behind `false` are already separable through `state(ticket)`
  (`Unbound` for an unknown or retired ticket, the settled state for one
  some earlier call terminalized), and reentrant mutation during `advance()`
  is a contract violation rather than an outcome. `mark_stale_if_version`
  adds a fourth and ordinary case — the version already matches — which is a
  no-change result, not a failure; a caller that retries on it retries
  forever.
- Queued-operation planning no longer scales quadratically with the
  operation count on per-chunk workloads. Hazard detection and
  parallel-phase grouping both compared every candidate against every
  operation accepted so far, which cost the most on the ordinary
  per-chunk-edit shape where the operations are pairwise disjoint and
  every comparison fails. Both now consult a chunk-keyed index and examine
  only the operations that actually share a chunk. Measured on an Apple
  M3 Max, planning a 256-chunk frame went from 58.4 us to 12.0 us and a
  4096-chunk frame from 22.99 ms to 208 us; the scaling matters more than
  either figure, since 16x the operations used to cost 394x the time and
  now costs 17.4x.
- Planning results are unchanged, including which operation a hazard
  blames, and `plan_operations` into a caller-owned report stays
  allocation-free in the steady state.
- The index is bounded, because it is the wrong structure for wide
  domains: an operation covering a whole domain would store one entry per
  chunk and make every later lookup walk one chain per chunk, where the
  scan it replaced rejected non-hazarding pairs on the field mask alone.
  Operations wider than 64 chunks therefore stay out of the index and are
  scanned as before. Whole-domain workloads — `resident_chunks()` is the
  default selector — measure about 10% slower than before this change, the
  cost of the bound on a shape that gains nothing from the index; the new
  `queued/plan_frame_dense_64` benchmark gates that number so it cannot
  drift.
- Plans under sixteen operations keep the all-pairs comparison, since
  `plan_parallel_execution_phases` must build its index per call and two
  allocations are not repaid by a handful of comparisons.
- Chunk-portal route selection now memoizes seam queries within a single
  selection, so the six axis orders and the greedy walk no longer re-walk
  the same chunk seam from the same tile. Route choice is unchanged; the
  `portal.scan_tiles` counter falls by roughly two thirds because that
  work no longer happens.
- `build_region_graph` returns `RegionGraphBuildResult` instead of the
  status-bearing `TopologyBuildResult`. It carries the same counts without a
  status, because that build cannot fail: the dense branch iterates keys
  `0..chunk_count`, so `InvalidChunk` cannot arise and `MissingChunk` does
  not exist under `AlwaysResident`, and the sparse branch builds from
  `resident_chunk_keys()`, which are in-world and resident by
  construction. `build_local_chunk_topology` and `update_region_graph`
  return `TopologyBuildResult`; the latter's `InvalidChunk` for an
  out-of-range dirty chunk is reachable.
- This is the same dead-status-channel defect as `save_world_archive`'s,
  and the type split is the same remedy. The measurable difference is what
  it removed: **45 assertions across six test files** compared a status
  that was invariantly `Built`. The type now makes them compile errors
  rather than relying on anyone to notice.
- The two branches that propagated an impossible status now `fail_fast`
  instead. If either ever fires, the residency assumptions the function
  rests on have changed, and continuing would publish a half-built graph.
- An update that falls back to a full rebuild converts the result,
  reporting `Built`.
- Both result types name their aggregate `topology_version_sum`; it is the sum
  of captured chunk topology versions, not a single `TopologyVersion`.
- `save_world_archive` now returns `WorldArchiveSaveResult` instead of the
  shared `WorldArchiveResult`. The shared type advertises the full 14-value
  `WorldArchiveStatus` enum, and save never wrote it — the field was
  invariantly `Ok`, so every symmetric `if (result.status != Ok)` a caller
  wrote against it was dead code. Worse than dead: it implied save had a
  failure channel, so callers did not guard the failure mode it actually
  has, which is the output vector exhausting memory. That is not
  representable as a returned status, so the new type states the contract
  at the declaration rather than implying a false one.
- `bytes_processed` becomes `bytes_written` on the save result. Source-breaking.
- 21 test sites asserted `save_world_archive(...).status == Ok`, which
  could not fail. They now assert `bytes_written > 0`, which can: a save
  that silently produced nothing would have passed the old form.
- Graduated maintenance tasks, budgets, metrics, opaque handles, explicit
  schedule/drain/release results, structural backend customization, registered
  scheduling, synchronous immediate execution, and the dense-and-sparse
  external chunk adapter to `tess::maintenance`. The stable adapter defaults
  to immediate execution; FIFO, queued-coalescing, dirty-bit, and the virtual
  scheduler remain experimental.
- Defined the enforceable 1.x source-compatibility boundary with an exhaustive
  installed-header manifest, named request/options/handle APIs, fail-fast
  lifetime and worker-pool misuse checks, prerelease-aware CMake selection,
  package evidence, release-tag-anchored snapshots, checksum-aware deep archive
  fuzzing, and an upgrade guide.
- Compatibility snapshots preserve header classes, direct aggregate
  membership, per-header documented public namespace-scope and `TESS_*` macro
  names, consumer/archive metadata, and release-tag immutability without
  maintaining a handwritten declaration-compatibility parser. Compiled
  consumers and integration builds provide the evidence for signatures,
  defaults, fields, overload resolution, aggregate use, and macro
  configurations. Consumer CMake projects have one canonical generated form
  rather than another interpreted compatibility model.
- Release snapshots are append-only and tag-anchored. Release evidence retains
  checksummed actual-version job logs, package validation uses C++20 without an
  unavailable compiler launcher, and compiler-floor checks fail closed.
- Worker-pool dispatch ownership covers plan-ordered result selection, even
  empty nested dispatches fail fast, and `DeltaCollector` self-moves or repeated
  moves invalidate or poison borrowed frames fail-closed.

### Fixed

- `EventStream` retirement no longer wraps a flow accountant's outstanding
  count. It subtracted its batch size directly where every other
  terminalization site routes through the zero-floored
  `record_left_outstanding`, so a shared accountant whose other flow
  terminalized first, or a `reset()` between publish and retire, drove the
  unsigned counter to about 2^64 and took the inventory and retention
  identities with it. Diagnostics only; simulation results were unaffected.
- `save_world_archive` writes `lattice_version` at a fixed width, and a
  lattice whose version cannot fit the header's 32-bit field is now a
  compile error rather than a file that saves `Ok` and never loads. The
  constant's own type decided the field width, and `LatticeType` requires
  only convertibility to `uint32`, so a custom lattice declaring a
  `uint64` version produced a header four bytes too long. Casting the
  write alone was not enough: a version above the 32-bit range would still
  have saved, because the truncated stored value can never equal the
  full-width trait the load compares it against. Both shipped lattices are
  unaffected.
- The A* fast paths saturate their route-cost arithmetic instead of
  wrapping, matching `best_chunk_portal`. Reaching the wrap needs distances
  beyond 2^32 tiles, so this is symmetry rather than a live defect.
- `DeltaCollector` sizes its coalescing table from the realized entity
  capacity rather than the requested one. Both probe loops rely on a null
  slot to terminate, and `reserve(n)` only guarantees `capacity() >= n`, so
  an implementation that over-allocated could fill the table and spin
  forever. No shipped standard library does; the guard is unconditional now
  rather than resting on that.
- A transition provider that emits a transition whose source lies outside
  the chunk it was asked about no longer grows the portal set without
  bound. Incremental removal keys on the source's chunk, so such a portal
  was never erased while every update touching that chunk appended it
  again, and incremental output diverged from a full rebuild. Previously
  only asserted, so the divergence was live precisely in builds with
  assertions compiled out; the transition is now dropped in every build.
- `ResolvedTransitionModel::for_each_dependency_chunk` rejects an
  out-of-world origin instead of emitting an out-of-range `ChunkKey`,
  matching the forward and reverse probes.
- PIBT priority inheritance no longer displaces an agent that has arrived
  or ended at `Unreachable`. The priority loop skips such agents and the
  apply pass checks the same condition before touching a stay-put agent,
  but inheritance reached them through the occupant path: passing traffic
  shoved them off their tile and rewrote them to `Blocked`, restarting a
  lifecycle documented as terminal. A second failure for one admission
  then broke the flow-accounting retention identity permanently. They are
  now treated like an agent standing on impassable terrain — the tile is
  claimed so later deciders are turned away, and the inheriting agent
  backtracks to its next candidate.
- A goalless agent standing on the origin tile no longer registers an
  arrival. `clear_path_agent_goal` zeroes `goal`, so the comparison could
  succeed for a journey that was never admitted, inflating `completed`.
- A `DirtyObservation` taken before a sparse chunk was evicted can no longer
  clear a dirty mark made after it was reloaded. Reloading restarts a
  chunk's `version` at zero, so version equality alone could match across
  two different residency intervals and erase work the observation never
  saw — the one thing the observation protocol promises cannot happen. The
  observation now carries the residency generation and a stale one is
  refused. Always-resident worlds are unaffected.
- `SparseWorld::ensure_resident()` returns an invalid handle for a key
  outside the bounded shape instead of writing past its slot table. It is
  the only residency entry point with no checked counterpart, and the
  directory's direct-slot mode indexes by key, so an out-of-range key wrote
  out of bounds wherever assertions were compiled out. It now behaves like
  `try_chunk()` and `try_meta()`, which already refused such keys.
- `dirty_chunk_domain()` and `active_chunk_domain()` yield chunk keys in
  ascending order on sparse worlds. They previously inherited residency
  order, which depends on load and eviction history rather than on world
  content, so identical content could iterate differently between runs and
  a non-commutative block kernel could produce different results. The
  underlying scans stay unordered; only the domain builders, which already
  allocate a vector, sort.
- Continuous-integration compiler caches no longer collide. Cache restore
  keys match by prefix, so the `dev` namespace also matched the sanitizer,
  cppcheck, and clang-tidy namespaces and restored whichever was written
  most recently. Every namespace is now terminated, and each cache-using
  job reports its own hit rate so a cold rebuild is visible in its log.
- Changes under `include/tess/ops/` select the paired benchmark run again.
  The directory was excluded as nanosecond-scale, which is true of its
  queued and scheduler families but not of the pool executor it also owns,
  whose benchmarks run at millisecond scale.
- The gated benchmark family list has one home. It was maintained by hand
  in the workflow, in CMake, in the contributor guide, and in a test that
  covered five of eighteen families, so a new family could ship a manifest
  and a target and still never gate. CMake now derives the set from the
  threshold targets it defines.
- The advisory clang-tidy profile pins its major version, matching the
  required gate. It installed the unversioned package, so the runner image
  decided which analyser ran and no pull request would have noticed a
  change.
- Every continuous-integration job declares a timeout. They inherited the
  360-minute default, which a hung job would spend at up to ten times the
  base billing rate on the macOS runners.
- `DiagnosticsSnapshot` no longer promises that all of its members are
  copies. `TraceRecord::label` is a `std::string_view`, and the trace API
  separately requires a label to outlive every reader — so the export
  header told consumers they had no lifetime obligation while the trace
  header said they did, and the ImGui panels dereference that view. The
  contract now states the exception, and what satisfies it.
- `path/node_index_space.h` moves from the public header set to the
  implementation set, where its detail siblings already are. Its entire
  body is `namespace tess::detail`, so it declares nothing a consumer can
  name — but `tess.h` and `path/path.h` do include it, and both header
  sets install to the same paths, so the file still ships and every
  existing include of it still resolves. The one behavioural consequence
  is inside the build: `INTERFACE_HEADER_SETS_TO_VERIFY` covers the public
  set only, so the header is no longer standalone-compiled on its own. It
  stays covered transitively through `tess.h`, which is swept. Its
  primary-template assertion also incorrectly described the
  `SparseResident` mapping as future work; that specialization is in the
  same file, so the message now describes the real condition — a custom
  residency policy needs its own specialization.
- `save_world_archive` and `load_world_archive` are now `[[nodiscard]]`,
  matching `inspect_world_archive`. In an exception-free library a
  returned result is the only failure channel, so
  `load_world_archive(world, bytes);` previously compiled and silently
  discarded `Corrupt`. Saving turned out to have no failure channel at
  all — see the `WorldArchiveSaveResult` entry — but its result still
  carries the written metadata and byte count, which no caller should
  drop by accident.
- Main-push CI collects its non-gating benchmark baselines in a
  dedicated job instead of inside Benchmark Gates. The gates job had
  been cancelled at its 45-minute ceiling on every full main push
  since 2026-08-07 — always in the final, non-gating baseline step,
  with the threshold gates themselves green — which failed CI Gate and
  stopped baseline artifacts from reaching the trends pipeline. The
  ceiling merged unvalidated because the long steps only run on push,
  never in pull-request CI. Baselines now run in parallel with their
  own budget, stay outside the merge gate by design, and still report
  through the ci-failure issue when they break.
- The budgeted-progress mixed-colony benchmark seats large populations:
  agent placement row stride is now configurable in the colony harness
  (`ColonyConfig::placement_stride`, default unchanged) and the mixed
  matrix places at stride 2, lifting the previous 227-agent seating
  ceiling that aborted population rungs above it. The binary also gains
  `--mixed-only` for resuming campaigns, seats the largest requested
  population up front before any cell runs, reports seated/requested counts
  when placement falls short, and line-buffers stdout so redirected
  campaign logs keep fatal stderr diagnostics in order.
- `cached_astar_path` accepts a `MissingChunkPolicy`. It previously
  hardcoded the impassable assumption with no override, so on a sparse world the
  cached call returned `NoPath` where the identical uncached call returned
  `Indeterminate` — adding a route cache for performance changed
  correctness semantics. `PathStatus::Indeterminate` exists specifically
  so a caller never mistakes "not searched" for "no route exists". The
  parameter now defaults to `ReportIndeterminate`, matching the safer default
  across public sparse-aware path entry points.
- The policy binds to the whole cache rather than joining the entry key,
  matching the existing `bind_class` and `bind_provider` precedent: a
  lookup under a different policy drops the cache and counts a
  `policy_rebinds` in `stats()`. Widening the key instead would duplicate
  `Found` entries, which are policy-invariant and are the entire
  suffix-index substrate, for a distinction that only affects the terminal
  status of searches that exhausted the resident set.
- The binding is normalized on dense worlds. No chunk can be missing under
  `AlwaysResident`, so the policy cannot change any answer there, and
  binding the caller's value would make a generic caller that alternates
  policies drop the cache for nothing. `policy_rebinds` is therefore
  always zero on a dense world.
- `Indeterminate` results stay cacheable. They are deterministic within a
  residency epoch — any evict, reload, or in-place edit changes the
  content-version fingerprint and drops the cache before a serve — and they
  come from the most expensive searches, so refusing to cache them would
  have traded a correctness trap for a silent performance one, aimed at
  exactly the callers who opted into correctness.
- `weighted_path_batch` accepts and preserves the same policy. When a shared
  reverse field touches a missing chunk, reached members still return `Found`;
  only unreached members fall back to independent A* classification. Grouping
  therefore cannot change a request's status.
- The change-point detector and the trend renderer now judge each
  benchmark on the metric its family is gated on, matching the paired
  sentinel confirmation. Both had read `cpu_time` unconditionally, so
  the `parallel/` families — which set `max_cpu_time_ns` to null on
  purpose, because pool work happens on worker threads and the
  dispatching thread's CPU time understates the operation — were
  alerted on and plotted using a number the gate deliberately ignores.
  A shift visible only in real time could not raise an alert, and an
  alert that did fire printed a confirmation command that measured a
  different metric than the one it reported. The selection rule lives
  in `tools/benchmark_thresholds.py` and is shared by all three tools.
  Twelve benchmarks change metric: the ten `parallel/` cells and the
  two manually timed path cache cells, whose CPU time is meaningless
  by construction. The switch cannot itself raise a false alert,
  because the detector rebuilds its whole window from the raw
  artifacts on every run and so re-reads the history on one metric
  rather than splicing two together.
- The benchmark change-point detector now distinguishes complete clean runs
  from partial coverage, reports every unevaluated candidate with structured
  reasons, and makes CI warn on partial analysis while rejecting unknown
  verdicts instead of silently treating them as successful no-ops.
- Prevented superseded CI runs from opening a failure issue when their only
  failure is the aggregate gate reacting to a cancelled dependency, while
  retaining the alert unless the Actions API confirms a newer equivalent push
  run.
- Successful failed-job-only reruns of default-branch CI now close the same
  bot-owned rolling failure issue as full reruns, without closing issues after
  human activity, cancellation, foreign-repository runs, or ambiguous reads.
- Restore the release-floor CI evidence by isolating ccache-free jobs from the
  workflow launcher, selecting the Visual Studio 2022 runner for the MSVC
  19.44 floor, and reporting every non-pull-request job failure in the rolling
  CI issue.
- Fix large-agent colony stutter by separating blocked retry exhaustion from
  reachability, adding deterministic backoff/jitter and bounded exact replan
  queues, and sharing an eight-query tick budget in the browser demo.
- Keep the browser colony live when completed agents seal a remaining goal:
  distinguish crowd-blocked legs from durable wall failures, turn the whole
  wave around together, and reject wall strokes on occupied tiles.
- Observers that hand out a span or reference into their own storage are
  now lvalue-only, so the dangling expressions they permitted are compile
  errors instead of undefined behaviour:
  - `OwnedChunkDomain::view/keys/begin/end` —
    `explicit_chunk_domain(keys).view()` returned a span into a vector
    destroyed at the end of the full expression. The deleted
    `chunk_domain(OwnedChunkDomain&&)` overload made this worse rather
    than better: a caller who hit its error would "fix" it by adding
    `.view()`, trading a compile error for undefined behaviour.
  - `ExecutionReport::plan/operations/find` and
    `ExecutionPlan::operations` —
    `plan_operations` returns by value, so the idiomatic-looking
    `for (const auto& op : plan_operations(world, ops).plan().operations())`
    iterated freed memory. `ExecutionPhase` already had a generation
    check, which made these unprotected accessors read as safe by
    comparison.
  The ban is conservative, and the release note should say so rather than
  claim only unsafe callers are affected: an observer cannot tell whether
  the span it returns will be consumed inside the same full expression or
  outlive it, so `explicit_chunk_domain(keys).view().size()` and
  `plan_operations(world, ops).plan().empty()` are rejected too even
  though both are safe. Binding the factory result to a named value fixes
  every rejected case, and it is the spelling that stays correct if the
  expression later grows. Observers that return a value rather than a
  borrow — `size()`, `empty()` — remain callable on a temporary.
- `explicit_chunk_domain`'s Doxygen claimed it "Copies, sorts, and
  deduplicates". It sorts and stops. `architecture/block.md` was always
  right, and the queued-operation layer deduplicates its own domains
  independently, so nothing depended on the promise — but a caller who
  believed it would have passed duplicates and got them back. The comment
  now says what the body does; the behaviour is unchanged deliberately,
  since deduplicating would be a silent semantic change.
- Not included: `DeltaFrame`. Holding one across a `publish()` aliases the
  vectors that become the pending accumulator, so the caller reads torn
  mid-tick data with no sanitizer signal. The audit proposed making it
  move-only, which does not fix it — a moved-into frame held across
  `publish()` tears identically. The fix is the generation gating
  `ExecutionPlan` already uses, which turns an aggregate of five spans
  into an accessor-gated view, and that is its own change.
- `DeltaFrame`'s documented lifetime was wrong in both directions and is
  corrected. It said a frame is valid "until the next mutating call on the
  collector (`begin_tick` / `record_*` / `collect_*` / `publish` /
  `clear`)". In fact `begin_tick`, `record_*`, `collect_*` and `clear()`
  fill or reset the *pending* buffers and never touch the published ones —
  which is the point of the swap in `publish()`, so the next frame can be
  recorded while the current one is applied — and `reserve()`, which
  re-reserves the published vectors and can reallocate them, was missing
  from the list entirely. The spans are valid until the next `publish()`
  or `reserve()`, and until the collector is move-assigned to or moved
  from, either of which replaces or empties the published vectors. (The
  collector became non-copyable in the same release; see that entry.) `header` is a value copy
  and outlives all of it. The same stale contract appeared a second time on
  `DeltaCollector`'s own Doxygen and a third time in
  `architecture/simulation.md` — the maintained page `surface.json` maps
  `DeltaFrame` to, so the one a consumer is actually sent to. All three are
  corrected.
- The two consumer-facing pages that teach the render bridge —
  `guide/presentation.md` and `getting-started.md` — now state the
  borrowing contract at all. Neither did. The guide recommends this branch
  for "different cadence, thread, or process" and for network mirrors,
  which is exactly the reader who would hold a frame across a publish, so
  it now says plainly that what crosses a thread or socket is applied
  shadow state or a copy of the records, never the `DeltaFrame`, whose
  spans point into memory the simulation thread is about to refill.
- The comment now also states the hazard it only implied: holding a frame
  across a `publish()` is outside the contract, and the resulting stale
  spans carry `first_tile`/`first_node` indices that can run past the
  `tiles`/`overlay_nodes` they index — and past the allocation itself if
  `reserve()` reallocated. In steady state the swap and clear never
  deallocate, so the read stays inside a live allocation; calling that
  "reads out of bounds" without qualification, as an earlier draft of this
  entry did, overstates it. Enforcing this is tracked
  as the remaining half of audit finding API3; this change makes the
  documented hazard accurate, which is the prerequisite for deciding
  whether to enforce it.
- A new `tess_delta_frame_lifetime_test` pins the narrowed contract, since
  a comment cannot fail.
- Runtime contract violations that previously became plausible idle or
  `NoPath` results in assertion-disabled builds now fail fast with API-specific
  diagnostics. `PathRequestRuntime::try_result` provides checked ticket lookup
  against transactionally published batches; schedule lifecycle and result
  accounting, resumable-queue reentrancy, and flow-owner rebinding are enforced
  consistently in debug and release builds. Representative public template
  failures now explain the required type or value.
- `detail::fail_fast` now prints its message unconditionally. It was gated
  on `TESS_ENABLE_DIAGNOSTICS`, so a release consumer — the one least able
  to reproduce the failure under a debugger — got a bare `abort()` with
  nothing naming what went wrong.
- Three preconditions that were `TESS_ASSERT` are now checked in every
  build, because in the builds that compile asserts out each one did
  something worse than nothing. `Schedule::task_stats` returned a
  default-constructed `ScheduleTaskStats`, which is all zeroes and
  therefore identical to what a registered task that has never run
  reports — the caller could not tell a bad id from an idle task.
  `Schedule::set_enabled` silently left the task in its existing state.
  `ResultChannel::value_for` asserted and then indexed anyway, which is
  undefined behaviour precisely where the assert was compiled out.
- `block.h`'s policy-view dispatch fails through `fail_fast` with a
  message instead of `assert(false)` plus a bare `std::abort()`. The
  program still terminated either way — the `std::abort()` was a separate
  unconditional statement, so `NDEBUG` removed the *diagnostic*, not the
  failure. What it removed mattered: under `NDEBUG` the consumer got a
  bare abort naming nothing, and the assertion honoured `NDEBUG` rather
  than `TESS_ENABLE_ASSERTS`, so it disagreed with every other check in
  the library about when it was live. It stays a
  runtime failure rather than becoming a `static_assert`, despite the
  condition being compile-time: the runtime-dispatching `for_each_chunk`
  instantiates this template for all four write policies whichever one the
  caller passes, so a `static_assert` would reject callbacks that accept
  only `ReadOnly` and never reach the branch.
- A new `tess_assert_ndebug_test` target compiles the assert suite with
  `NDEBUG`. Without it the new death tests would have passed against the
  old code too, since with asserts enabled both forms abort — the existing
  NDEBUG contract cell compiles but never runs, so it could not tell them
  apart.
- The decision-guide route-map diagram renders again: its Mermaid `accDescr:`
  line had been wrapped to a second line, which Mermaid cannot parse, so the
  page showed raw diagram source. The style guide now records that Mermaid
  `accTitle:`/`accDescr:` lines are exempt from the 80-column guideline.
- Right-align Dear ImGui timing values and accumulate the WebAssembly demo's
  timing statistics across frames so sample, average, minimum, and maximum
  columns remain meaningful at browser clock granularity.
- Stabilized the Dear ImGui timing panel with independently clipped table
  columns, preventing live counter digit changes from shifting neighboring
  timing metrics every frame.
- `TESS_ENABLE_ASSERTS` and `TESS_ENABLE_DIAGNOSTICS` now carry
  `#pragma detect_mismatch` on MSVC, matching what `core/config.h` already
  did for the exception mode and `core/capacity.h` for the internal
  capacity hook. Both change the program's shape across translation units
  — the first rewrites 14 inline function bodies in `storage/world.h`
  alone, the second makes `PathCounters`, `TraceBuffer`, `WarningSink` and
  six more types exist or not — so defining either inconsistently violates
  the one-definition rule with no diagnostic on any compiler. These are
  the two macros consumers actually set, and `integration-policy.md`
  actively tells them to set the first, so they were the two most worth
  guarding and the two that were not.
- `integration-policy.md` states the obligation and what the guard does
  and does not cover: MSVC gets a link error, GCC and Clang have no
  equivalent mechanism and consistency there is the build system's job.
- A new `test_macro_odr_guards.py` asserts every build-wide macro switch
  has a guard, that each stamps both states — a pragma on only one branch
  cannot catch the mismatch it exists for, since the unstamped branch
  contributes no symbol to disagree with — and that each sits inside a
  `_MSC_VER` block. The test reads source rather than compiling, because
  the pragma is MSVC-only and a link check would pass vacuously on every
  other toolchain.
- Benchmark configuration no longer requires Git or a `.git` directory: the
  maintenance-campaign benchmark resolves its embedded source identity
  through a dedicated CMake module — an explicit
  `TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA` cache value wins, Git resolves
  `HEAD` when available, and otherwise a non-admissible sentinel is embedded
  that campaign evidence staging rejects — so source archives and system or
  preprovided Google Benchmark installs configure cleanly.
- The mixed-colony benchmark's lateness percentiles now cover every
  completed cohort item (zero when on time) instead of only late
  completions, matching the family's declared `completed_cohort_items`
  sample base; previously the published percentiles silently described
  just the few late items.
- Restore warnings-as-errors builds on the MSVC 19.44 floor by expressing
  dependent constant conditions as compile-time branches.
- Guard exact path-agent replan queue draining against an inconsistent internal
  index so optimized GCC 12 no-RTTI builds pass warnings-as-errors without
  changing valid FIFO behavior.
- Avoid running the full local native test suite solely because a branch is
  being pushed to its remote for the first time; verified new branches now use
  the locally tracked remote default branch for path-based test selection.
- Reused registered schedulers' nonzero owner epochs for active-operation
  identity, avoiding a GCC dangling-pointer diagnostic for stack-local
  experimental adapters without weakening cross-scheduler reentrancy checks.
- Bound the uncached Xcode 16 release-floor build to the hosted runner's three
  CPUs so compiler oversubscription cannot exhaust the job timeout.

### Performance

- Per-frame queued planning is measured. `queued/plan_frame_256` and
  `queued/plan_frame_4096` time `plan_operations` plus
  `plan_parallel_execution_phases`, which nothing timed before: every
  other queued benchmark plans outside its measured loop, and the one
  in-loop planner call plans a single operation. Two sizes are registered
  so growth reads as a shape rather than a point. First readings, on an
  Apple M3 Max: **59.6 us at 256 operations and 23.4 ms at 4096** — 16x the
  operations for 392x the time. No fix is in this change; the measurement
  comes first so the fix has before-and-after evidence.
- Field-product cache scanning is measured against resident entry count.
  `fields/cache_scan_entries_8` and `_128` hold per-store work identical —
  same world, same goal cardinality, same product build — and differ only
  in how many entries are resident when the cache's linear scans run. A
  miss-and-store walks three of them (lookup, the store's existing-key
  scan, then eviction), all linear in entry count, so the delta is their
  aggregate rather than eviction alone. The family's other cache
  benchmarks hold about two entries, so those scans never had more than
  two candidates to compare. The 128-entry variant is registered as a
  paired sentinel: the scans are only about 7% of each reading, so the
  bootstrap ceiling gives trend visibility rather than a complexity gate,
  and the paired run's relative effect floor is what can actually see a
  scan regression.
- Field-product stores no longer discard the caller's buffers or rescan the
  cache to recover what they just stored. `store` took the product by move,
  leaving the caller's member empty, so the next rebuild reallocated a
  world-sized distance array — about 1 MiB on a 512x512 world, on every
  world edit, cache eviction or provider revision change. The caller then
  had to `lookup` the product back, which rescanned every entry and
  recorded a cache HIT for work the cache had not reused, inflating the
  published hit rate by one on every build. `store_reusing` returns what it
  stored and hands the caller the displaced entry's storage, cleared. The
  rvalue `store` overloads keep their `bool` contract; their argument is
  still left empty, though on a displacing store it now owns the displaced
  buffers rather than its own. The reuse covers stores that displace
  something — a same-key replacement, or an admission the byte budget makes
  evict. An admission into a cache still under budget displaces nothing, so
  that rebuild allocates as before; a runtime only stops reallocating once
  its product cache is full. Measured on a 64-tile world: rebuilding after
  a displacing store went from five allocations to zero. The removed
  relookup, by contrast, is saved on every build.
- A calibrated M3 and Steam Deck campaign keeps the registered dirty-bit
  maintenance scheduler experimental: M3 was flat, while Steam Deck immediate-
  execution guardrails materially regressed. The separately validated task,
  handle, result, external adapter, and immediate-execution contract remains
  eligible for v0.13 promotion.
- Weighted searches order their open lists with a packed 64-bit key
  (one compare instead of up to three field compares per heap step).
  The ordering is bit-identical — a strict total order isomorphic to
  the previous comparator — so paths, costs, expansion counts, and
  determinism are unchanged. On Apple M3 the weighted A* batch cells
  drop 13-14% and the weighted distance-field batch 11%.
- Four unmeasured paths now have benchmarks, so a change to them moves a
  number. `collect_render_tile_deltas` — which the scheduler calls every
  tick — had none at all, and the existing `render_delta/*` family
  exercises a different collector; the new pair holds the chunk count
  fixed and varies only the dirty count, showing that 256x the dirty work
  costs 1.66x the time because the full chunk sweep dominates.
  `spatial/local_coordination` gains a second size, because its cost is
  quadratic in the reservation count and one fixed size cannot separate a
  constant-factor regression from an exponent change; 4x the requests cost
  8.2x the time. The `fields/*` product family gains a 512x512 world, its
  first memory-bound point — every other cell fits in L1/L2, so layout and
  per-build allocation changes were measured only where memory traffic is
  free.
- The paired sentinel representing `include/tess/sim/` measured thread
  creation rather than the code that directory owns, and was the widest
  interval of the twelve sentinels; it is replaced by a compute-bound cell.
- `UnitRouteCache` no longer copies its whole suffix slot table before
  reassigning it. No measurable effect on the owning benchmark family, which
  is recorded as such rather than presented as a win.
- Portal-route seam scans resolve their two chunk pages once and walk
  local tile ids instead of running a full coordinate resolution per seam
  tile. Portal selection is identical — iteration order, scoring,
  tie-breaking, and scan accounting are unchanged, and the per-tile loop
  remains the authority for out-of-shape or non-resident chunks. On the
  Steam Deck the goal-churn portal tick drops 21% and candidate selection
  11%; Apple M3 is flat, plausibly because the wider core already hides
  the resolve arithmetic behind the seam-tile loads.

### Documentation

- Documented that a raw field write does not make a region graph stale.
  Only `mark_topology_dirty` and `mark_topology_rebuilt` advance the
  topology version that freshness compares, so editing a field a movement
  class or its provider reads — opening a wall, placing a stair — leaves a
  built graph reporting fresh, and `precheck_path` can then return a
  definitive, wrong `Unreachable` that makes a caller skip a search which
  would have succeeded. The obligation is now stated on `precheck_path`,
  on `StairTransitions`, and in the topology architecture note.
  `precheck.h` previously said a graph that no longer matches the world
  reports `GraphStale`, which overstated what the check can detect.
- Corrected the robotics use case: `examples/stairs_3d.cc` demolishes its
  stair with a direct field write and hands the affected chunk key to
  `update_region_graph`. It was described as a queued edit that marks the
  region dirty, which is machinery that example does not use.
- The root agent-instructions file (`AGENTS.md`) now documents all three
  changelog fragment streams and links the contributor workflow docs; it
  previously directed contributors to hand-edit
  `docs/decisions/CHANGELOG.md`, which repository policy prohibits. A
  one-line `CLAUDE.md` importing `AGENTS.md` makes the same instructions
  load in Claude Code sessions, which read only `CLAUDE.md`.
- Per-test documentation moves from the single `tests/AGENTS.md` catalogue
  into per-test fragments under `tests/agents.d/`, with the CI drift gate
  now enforcing an exact bidirectional mirror of the test set (including
  the pytest suites the old gate could not see). Benchmark-binary
  conventions move to `bench/AGENTS.md`, and `CLAUDE.md` import shims make
  the agent instructions load in Claude Code sessions.
- Per-test documentation fragments now state each test's purpose and preserve
  only non-obvious traps, rationale, deliberately narrow claims, and
  load-bearing coverage gates. Behavior inventories remain authoritative in
  the test sources, reducing an unverified documentation drift surface.
- The CI gate inventory in `CONTRIBUTING.md` now records which tier runs
  each check — pull request, pull request when the change classifier
  selects it, main-only, or advisory. The list previously read as though
  a pull request were checked under TSan, release, macOS and full-tree
  clang-tidy, none of which run there. Tiers were read from
  `.github/workflows/ci.yml` and `tools/ci_changes.py` rather than from
  the surrounding prose. The exception-free contract jobs, which the
  inventory omitted entirely, are now listed, and CodeQL is recorded as
  deliberately advisory.
- Four documentation statements that contradicted the code or another
  maintained page are corrected. The getting-started tutorial said the
  parallel executors are prototypes and that every published performance
  median is single-threaded; the worker pool is the production parallel
  backend and `performance.md` publishes four-worker figures, so a reader
  following the concept ladder was architecting around a serial-only
  assumption. The path architecture note said `store_checked` reports
  pre-allocation capacity failure, but it captures the candidate entry's
  dependencies — which allocates — before the status comes back, as the
  exception-free note already recorded. The pathfinding guide said all
  shipped routing "will not spread or queue a crowd" without mentioning the
  two shipped movement-commit tiers that resolve contention. Both are
  documented in the simulation architecture note, but neither appeared in
  the pathfinding guide nor in the roadmap's shipped list, which
  `guide/README.md` designates authoritative; both now name them, with the
  swap policy stated rather than assumed. And `ScheduleTaskDesc` was
  described as a phase and cadence, omitting the required static-storage
  name.
- Documentation tooling statements now match the tooling. The style guide
  told contributors to add headers to an opt-in `DEFAULT_HEADERS` list that
  has been derived from the CMake header sets since `ee76d98` — an
  instruction that could not be followed, describing the gate as opt-in
  when it covers every installed header. It also now states the gate's real
  floor: it does not validate members, and it accepts an undocumented
  declaration when another with the same normalized signature is
  documented. `docs/llms.txt` gained the integration-policy page, the one
  that answers the exceptions, RTTI, determinism, and allocation questions
  an integrator asks first and the only nav-level page it omitted. And the
  concepts index no longer carries a second, unenforced copy of the TDD
  index, which had already drifted one entry behind the real one.
- The example smoke gate in CI asserted that at least 13 examples ran while
  the `dev` preset builds 14, under a comment claiming every example runs.
  An example could stop building and the gate would still pass. It is now
  an exact count.
- Internal milestone labels (`M5`, `M10`, `M11`, `S5.3`…) no longer appear
  in the Concepts pages, which `for-agents.md` designates normative. They
  were planning vocabulary that resolved to nothing a reader could look up.
- A new `tools/check_doc_commands.py` gate validates the build commands the
  docs quote. The snippet checker byte-synchronizes C++ fences with compiled
  sources, but shell and CMake fences had no equivalent, so a renamed preset
  or a removed target would leave a command that fails for the reader with
  nothing failing in CI. It resolves every `--preset` against
  `CMakePresets.json` and every `--target` against the declared CMake
  targets — statically, because executing them would just rebuild what CI
  already builds.
- The benchmark trend snapshot is regenerated from the ten baseline
  snapshots on the `benchmark-data` branch. It had been pinned to a
  2026-07-12 CI run — 138 commits behind, spanning a confirmed regression
  and its fix — while the page described it as possibly "stale by a few
  commits". The page now names the run and commit the data comes from and
  says plainly that it lags main between regenerations.
- The installation page said the development dependencies are "fetched only
  by developer presets". They are fetched by the options that enable them:
  `TESS_BUILD_TESTING` defaults to on for a top-level build, so a bare
  `cmake -B build` in a clone fetches GoogleTest at configure time — which
  fails on a restricted network, for an evaluator who has just read that it
  would not happen. The page now says so and gives the two fetch-free
  routes.
- Three documents stated three different include-surface policies, and the
  tool cited as enforcing one of them enforces neither. There is now one
  policy: the header manifest classifies stable, optional-stable,
  experimental, and implementation-only headers; only the first two classes
  carry 1.x compatibility guarantees, with the narrowest owning stable header
  preferred in compile-sensitive code.
  `tools/check_public_surface.py` is described as what it is — a symbol
  manifest gate, not an include policy.
- Improve documentation accessibility with a named search dialog, distinct
  diagnostics navigation labels, underlined inline links, and WCAG-compliant
  text contrast in both site color schemes.
- Load the pinned Mermaid runtime only on documentation pages that render a
  diagram, preserving self-hosting and instant navigation while removing about
  963 KiB from ordinary page loads.
- The three `merge_planned_dirty` overloads disagree on `noexcept`, and
  each now documents why at its declaration. The audit read the
  disagreement as an inconsistency; it is the contract. An overload is
  `noexcept` exactly when it does not allocate: the accumulator overload
  merges already-populated records into the world, while the scratch and
  partitions overloads reserve their destination first and can therefore
  throw `std::bad_alloc`.
- Making them agree would have been the wrong fix — marking an allocating
  function `noexcept` converts a `bad_alloc` into `std::terminate`. The
  useful change is that an `-fno-exceptions` consumer can now decide
  whether a call can throw by reading the signature and the note beside
  it, instead of reading the body.
- A test pins the split, so it cannot drift back into looking accidental.
- The optimization log is now assembled from per-experiment fragments in
  `docs/planning/optimization-log.d/` rather than edited in place, the same
  mechanism the changelogs use. Every performance branch appended to the top
  of one file, so concurrent branches conflicted on every rebase, and the
  file is subject to the repository's 24,000-token limit — which it has
  exceeded twice, forcing an archive split each time. Fragments remove both
  problems: a branch adds a file nobody else touches, and the maintained log
  only grows at release.
- Added a source-synchronized pathfinding strategy comparison backed by one
  compiled example and the existing paired benchmark workloads.
- Embed a WebAssembly pathfinding-strategy demo that runs the native example's
  shared C++ model and contrasts independent A*, exact route caching, weighted
  batching, and caller-reused distance fields on one obstacle course with
  accessible call sequences, operation-specific counters, and truthful route
  versus map-wide field representations.
- Refocused the README on a plain-language capability overview, simpler
  integration choices, and a compiled quickstart that prints its returned
  path directly.
- `integration-policy.md` gains a "Residency coverage" section naming the
  four dense-only families — the queued-operations layer, the weighted
  distance-field products, the portal-route products, and the PIBT tier —
  and stating the concrete change to expect when they absorb sparse: the
  `MissingChunkPolicy` parameter the sparse-aware path entry points
  already carry. All four `static_assert` on `AlwaysResident` with
  messages that incorrectly described sparse support as future work, while
  sitting in the ordinary public namespace, so a consumer had no single place to learn
  which parts of the surface do not compile against a
  `SparseResidentWorld`.
- The audit also found that `weighted_path_batch` *is* residency-generic but
  did not expose its missing-chunk behavior. It now accepts the same explicit
  `MissingChunkPolicy` as the other sparse-aware search entry points.
- The audit offered relocating these families to
  `include/tess/experimental/` as the alternative. That is the wrong half
  of its own either/or: they are production-promoted and tested on dense
  worlds, and the `static_assert`s mark a residency limitation rather than
  experimental status. Relocating would churn every consumer include to
  signal a maturity difference that is not the actual distinction. Being
  outside `experimental/` is explicitly not a stability promise —
  `support.md` makes every `0.x` release pre-stable, and this section says
  so rather than implying otherwise.
- The roadmap now distinguishes `v0.12.0` capabilities from changes landed
  only on `main`, release-gated v0.13/1.0 work, and future extensions, with the
  unreleased fragments as the complete main-only change inventory. The README
  and site landing page no longer describe the whole checkout as the release,
  the planning index separates maintained status from implementation and
  point-in-time records, and stale status or ownership text no longer
  describes completed work as pending or authoritative for the future.
- Give the documentation homepage and key entry pages descriptive C++
  pathfinding titles and summaries, and identify the documentation subdomain
  as tess through homepage `WebSite` and Open Graph metadata.
- Improved public discovery metadata, pathfinding guidance, and crawler
  sitemap hints without adding a competing pathfinding landing-page URL.

## [0.12.0] - 2026-08-05

### Added

- Exception-free consumer builds. Clang-family and GCC consumers may compile
  every public header, aggregate, and example with `-fno-exceptions`, and
  native MSVC consumers with `/EHs-c-` and `_HAS_EXCEPTIONS=0`. The installed
  `tess::tess` target stays neutral; `TESS_HAS_EXCEPTIONS` and
  `tess::has_exceptions` report the compiler mode and cannot be overridden.
  Every translation unit in a program must use the same mode. See
  [exception-free builds](docs/architecture/no-exceptions.md).
- Non-throwing capacity entry points for exception-free callers:
  `BlockScratch::reserve_bytes_checked`,
  `WeightedPortalSegmentCache::reserve_segments_checked`,
  `reserve_path_nodes_checked`, and `ClassView::store_checked`, reporting
  through `ReserveStatus` and `PortalSegmentStoreStatus`.
- Explicit no-throw execution aliases `NoThrowWorkerPoolPhaseExecutor` and
  `NoThrowScopedThreadPhaseExecutor`, plus the `ScheduleNoThrowTaskFn` erased
  signature, so an explicitly `noexcept` callback keeps that property through
  the queued, result-channel, schedule, and auto-exec adapters.
- A Conan recipe and a vcpkg checkout overlay alongside the existing
  `FetchContent` and installed-package paths. See
  [packaging](docs/packaging.md).
- Per-tick timing and allocation attribution. Diagnostics-enabled schedules
  time the complete tick and each executed task under its static label,
  duration records carry inclusive allocation and deallocation byte deltas,
  and snapshots retain the newest trace records with a dropped count.
  Diagnostics-off builds retain no timer or attribution code.
- A resolved transition model shared by exact paths, reverse fields,
  multi-goal products, topology, caches, path agents, and movement commit,
  including clearance-preserving diagonal steps, axial-hex adjacency, and
  provider-composed special edges.
- Compile-time compact-cost range assessment and explicit runtime
  `CostOverflow` results.
- Typed queued intents, cooperative generation-stamped async tickets, bounded
  exact event streams, and event/background scheduling adapters.
- Lazy block pipelines and exact allocation-free box, radius, and chunk-span
  queries.
- Deterministic coarse region/portal routes, persistent weighted field
  products, caller-keyed area indexes, tactical assignment, and local move
  coordination.
- Versioned authoritative world archives, an optional Flecs adapter, and
  bounded optional Dear ImGui world inspection/edit-intent helpers.
- An optional stable-C-API WebGPU transport with generation-bearing resources
  and bounded asynchronous readback.
- A network-free external-grid parser and independent oracle harness; external
  corpus acquisition remains gated on documented content rights.

### Changed

- Path results now report their fixed-point cost scale; provider type and
  revision participate in persistent path-product and cache identity.
- **Behavior change:** `collect_planned_dirty` and both partition-collecting
  `merge_planned_dirty` overloads no longer throw `std::length_error` on a
  record-count overflow. They now return the new
  `PlannedDirtyCollectStatus::CapacityExceeded` and
  `PlannedDirtyMergeStatus::CapacityExceeded` values instead, in
  exception-enabled builds as well as exception-free ones. Callers that
  relied on the exception must check the returned status; exhaustive
  `switch` statements over either enum need the new value. `AutoExecTask`
  absorbs the status internally and still publishes every started
  callback's dirty metadata through its allocation-free fallback merge, so
  its observable result is unchanged.
- The consolidated public surface is versioned and released as `v0.12.0`.

### Fixed

- Occupancy-blocked path agents retry retained steps without repeated
  occupancy-blind searches, stop after a bounded retry budget, and surface an
  explicit terminal outcome. The colony demo reports those outcomes.
- Special-transition field products preserve provider costs, transition
  enumeration propagates callback failures, and zero-step agent ticks preserve
  blocked-retry budgets.
- Archive loads invalidate pre-load cache identities, area-index validation is
  constant time, and reentrant queued-work mutation is rejected safely.
- The external-grid harness now parses scenario lengths on Apple libc++ and
  rejects incompatible required-data options before probing the toolchain.
- Persistence decoding, checksum handling, and field validation now compile
  cleanly across the supported GCC, Clang, MSVC, and cppcheck gates.
- The cppcheck gate now bypasses cppcheck 2.21 template-simplifier crashes
  while retaining product-header analysis and compiler test coverage.

### Performance

- Every literal benchmark in a threshold-gated family is covered by a
  calibrated or explicitly labeled bootstrap ceiling; newly covered
  resolved-transition, weighted-product,
  coarse-topology, area-index, and Flecs workloads close the prior gate gaps.
- Default orthogonal unit routes, fields, and product replays retain their
  direct specialized paths while other lattices, step policies, and providers
  use the resolved transition model.
- Indexed axis-neighbor iteration remains inline in hot reconstruction loops,
  and bounded weighted floods hoist per-node bucket work out of their
  per-neighbor loop.
- Fully covered sparse worlds bypass residency hashing on the storage read
  path.
- Default orthogonal distance-field products capture dependencies at
  chunk-frontier level again instead of enumerating exact transitions per
  reached tile, undoing a v0.12 build/store regression.
- The serial-versus-pool dispatch crossover is measured and published rather
  than estimated; the pool's dispatcher no longer shares a CPU with a worker.

## [0.4.0] - 2026-07-20

### Added

- Curated `<tess/pathfinding.h>` and `<tess/simulation.h>` facade headers;
  the existing `<tess/tess.h>` compatibility umbrella remains available.
- A compiled quickstart, tracked installed-package and `FetchContent`
  consumers, and source-backed documentation snippets enforced in CI.
- CI verification that the quickstart's documented output matches the
  compiled binary.
- A strict MkDocs site deployed through GitHub Pages, plus a single-threaded
  interactive pathfinding example compiled with Emscripten 6.0.3.
- Support, security, and structured issue-reporting metadata, a Contributor
  Covenant code of conduct, and weekly Dependabot updates for GitHub Actions
  and pip dependencies.

### Changed

- Package metadata and maintained documentation now report `0.4.0`
  consistently.
- The README now leads with fit and non-fit guidance, a complete runnable
  program, dependency-free example commands, and explicit install-prefix and
  `FetchContent` instructions.

## [0.3.0] - 2026-07-17

### Changed

- BREAKING pre-release hardening of the queued-operation and path-cache
  surfaces: `PlannedOperation` gets checked, immutable construction with
  a world-shape stamp; `ExecutionPhase` becomes a planner-issued,
  generation-stamped capability so hand-built or stale phases cannot
  bypass parallel ownership checks; deferred dirty recording and merge
  return explicit failure results and reject cross-world use; portal
  segment construction and compaction commit transactionally, cache
  budget reductions apply immediately, and result hooks are `noexcept`.
- Version metadata now has one CMake authority that generates the
  installed `tess/version.h`; dependency acquisition is pinned by
  default with hash-verified tooling.
- CMake floor lowered to 3.25 (3.28 and newer keep module-scan
  suppression and fetched-dependency hygiene), and the project declares
  the `3.25...3.28` policy range.
- README restructured as a user-facing overview with a features list, a
  quickstart, and measured performance figures; contributor material
  moved to `CONTRIBUTING.md`.
- Docs indexes lead with maintained material; the TDD archive and
  planning records are marked historical.

### Added

- A `consumer` CMake preset: headers-only configure for installing the
  library with no tests, examples, benchmarks, warnings-as-errors, or
  network fetches.
- An opt-in `tess_docs` Doxygen target (`TESS_BUILD_DOCS=ON`) generating
  a local HTML API reference.
- Top-level `CONTRIBUTING.md` (developer workflow, quality gates,
  benchmark policy) and this `CHANGELOG.md`.
- `docs/getting-started.md`: a tutorial from shapes and schemas to the
  schedule loop and render bridge.
- GitHub Releases published for the existing `v0.1.0` and `v0.2.0` tags.
- Sparse local topology reports `MissingChunk`; stateful transition
  providers expose a monotonic revision.

### Fixed

- Deterministic allocation-failure testing reports itself unavailable
  (and stays inert) under MSVC checked iterators instead of terminating;
  Windows keeps failure coverage in Release.
- Cross-platform warning debt cleared across GCC, Clang, AppleClang, and
  MSVC.

## [0.2.0] - 2026-07-12

### Changed

- ChunkMeta hot/cold SoA split (M5): flag words and dirty bounds moved
  to world-owned columns with new `dirty_flags`, `active_flags`, and
  `dirty_bounds` accessors. Breaking versus the undocumented struct
  layout; minor bump by decision.

### Added

- Per-agent pathing dirt: `PathSubmitScope` plus `PathAgentRoutes`
  retained routes, so one goal re-arm no longer replans the whole batch
  (4.2x on the goal-churn tick benchmark).

### Performance

- The audit remediation stack: de-elided benchmark gates, batch
  grouping and settle-target floods (~118x near-goal), scheduler and
  planner overhead cuts, worker-pool claiming (~2x), and intrusive LRU
  eviction (3.5x).

## [0.1.0] - 2026-07-11

### Added

- The initial pre-stable surface, complete across milestones M0-M15:
  constexpr shapes with one model for 2D, vertical 2D, and 3D;
  chunk-local SoA storage with sparse residency; queued operations with
  result channels and write-policy enforcement; the schedule with
  cadences, budgets, auto-exec, and a selectable parallel phase
  executor; movement classes with per-class topology, transition
  providers, and the region-graph precheck; A* and weighted routing
  with route and field-product caches; distance-field products and the
  byte-budgeted cache; the ECS adapter (EnTT-gated); the versioned
  DeltaFrame render bridge; compile-gated diagnostics with ImGui
  panels; and the GPU backend interface (interface only).

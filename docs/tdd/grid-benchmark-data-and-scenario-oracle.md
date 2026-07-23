# TDD: External grid benchmark data and the scenario oracle

## 1. Status and relationship to other TDDs

This TDD covers the test and benchmark harness only; implementation
status is tracked in the roadmap, not here. It adds no public API, no
headers under `include/tess/`, and no new library semantics.

It depends on, and deliberately does not duplicate,
[Lattices and the resolved transition model](lattice-and-transition-model.md):

- The full scenario oracle (Section 10) requires that TDD's Phase B
  (`DiagonalSteps<RequireBothClear>`, fixed-point 128/181 costs, integer
  octile heuristic). The degraded orthogonal mode in Section 10.1 is the
  only part with no Phase B dependency.
- The structured-map benchmarks here complement, and can serve as fixtures
  for, that TDD's Section 20 benchmark plan (corner-heavy mazes, both
  clearance masks).
- The corner rule this oracle pins (Section 10.2) must match the external
  data's published assumptions, not implementation convenience.

Coordination note for the lattice implementation: the external scenario
optima assume diagonals may not pass a blocked corner, which corresponds to
`RequireBothClear` and only that rule. Oracle assertions are invalid under
`RequireOneClear`.

## 2. Summary

Adopt a small curated subset of the community 2D grid pathfinding
benchmarks — Sturtevant's Moving AI map and scenario sets, consumed from the
Shortest Path Lab mirror at a pinned revision — as opt-in inputs to the tess
test and benchmark harness. The data provides two things the current
synthetic-only harness cannot:

- **Structured-map coverage.** Real game maps, mazes, rooms, and random
  fields exercise open-list behavior, tie-breaking, and expansion patterns
  that open synthetic grids structurally cannot.
- **An independent correctness oracle.** Scenario files carry thousands of
  start/goal pairs with externally computed optimal path lengths. They can
  catch inadmissible heuristics, corner-rule bugs, and suboptimality that
  tests written alongside the implementation share blind spots with.

The same maps also serve as structure-rich fixtures for the chunked and
sparse machinery: chunk-size equivalence sweeps, topology and precheck
fixtures, and sparse stream-and-retry convergence (Section 11).

The data is fetched on demand, checksum-verified, cached outside the
repository, and never vendored. Everything degrades to a skip, never a
failure, when the data is absent.

## 3. Motivation

Every published tess pathfinding number today runs on synthetic grids, and
the flagship A* figure is an open 512x512 corner-to-corner search. Open
grids barely stress the frontier: the heuristic is near-perfect, expansion
counts stay near the path length, and tie-breaking hardly matters. A
regression or a bug that only manifests on corridor-heavy or room-structured
maps is invisible to the current calibrated ceilings and to the current
tests.

The lattice TDD's diagonal movement work raises the stakes: an octile heuristic
that is fractionally inadmissible, or a corner rule that is subtly
permissive, still returns plausible paths on hand-written fixtures. An
external corpus of optimal costs computed by independent implementations is
the cheapest strong check available, and the standard map names make
published tess numbers legible to anyone who has read a pathfinding paper.

## 4. Goals

- Reproducible, checksum-verified acquisition of a curated grid benchmark
  subset at a pinned upstream revision.
- Licensing and attribution handled deliberately: only data tess may use,
  with the required citation recorded where numbers are published.
- A loader contract from `.map`/`.scen` files onto tess worlds that fails
  loudly on anything it does not model.
- A scenario oracle with a degraded orthogonal mode that has no lattice
  dependency, and a full octile mode gated on lattice Phase B.
- Structured-map benchmark entries under the existing calibrated-ceiling
  methodology.
- Reuse of the same data as equivalence fixtures for chunk layout,
  topology, and sparse residency, with no additional acquisition.
- Zero impact on the default build, test, and benchmark presets when the
  data is absent: no network access, no failures, explicit skips.

## 5. Non-goals

- **No vendored data.** No map or scenario file is ever committed to this
  repository, regardless of size or license (Section 6.3).
- **No public map-loading API.** The loader is harness support code; if a
  shipped import surface is ever wanted, that is a separate design.
- **No octile semantics.** Movement rules, costs, and heuristics are owned
  by the lattice TDD; this document only consumes them.
- **No MAPF participation.** The Shortest Path Lab MAPF tracker
  (tracker.pathfinding.ai) benchmarks coordinated multi-agent planning,
  which the roadmap defers entirely; single-agent scenario data is the
  correct scope for tess.
- **No solver-comparison benchmarks in the repository.** Maintained
  head-to-head benches against research solvers (HOG2, Warthog) are
  rejected in Section 16.5; a one-off offline calibration is permitted and
  belongs in the optimization log, not in CI.
- **No mesh, polygon, road, or voxel sets** in the initial scope, though
  the acquisition design does not preclude them.

## 6. Data source, licensing, and attribution

### 6.1 Upstream

The canonical community gateway is benchmarks.pathfinding.ai (Shortest Path
Lab, Monash University), whose data lives in the version-controlled
repository `bitbucket.org/shortestpathlab/benchmarks`. Its
`grid-maps/movingai/` tree mirrors Nathan Sturtevant's Moving AI grid
benchmarks (movingai.com), Version 2 (ca. 2018), as per-map `.map` files
with per-map `.map.scen` scenario files.

tess consumes the mirror, not movingai.com, because the mirror is a git
repository: an exact revision can be pinned, individual files have stable
raw URLs at that revision, and upstream changes are diffable. The initial
pin is:

```text
shortestpathlab/benchmarks @ fe6351b0700a0f4e75d0bd79ce3bf5478bc60c94
```

### 6.2 Licensing facts

As stated on movingai.com/benchmarks/grids.html at the time of writing:

- The benchmark data is provided under the Open Data Commons Attribution
  License (ODC-By), with the request that users cite: Sturtevant, N.,
  "Benchmarks for Grid-Based Pathfinding", Transactions on Computational
  Intelligence and AI in Games, 4(2), 144-148, 2012. ODC-By is a
  database-rights license: it covers the collection, not the copyright in
  individual contents.
- BioWare granted permission to distribute the maps derived from its games
  (Baldur's Gate II, Dragon Age: Origins, Dragon Age 2) for research
  purposes; that permission does not clear the underlying content
  copyright for arbitrary downstream use.
- Permission has **not** been acquired for the other commercial game sets
  (StarCraft, Warcraft III), which are hosted on a remove-upon-request
  basis.
- The synthetic sets (mazes, rooms, random) are generated data with no
  third-party content, encumbered only by the ODC-By attribution terms.

### 6.3 Decisions

- **Synthetic sets first.** The initial manifest draws exclusively from
  the synthetic sets, whose provenance is unencumbered. The
  BioWare-permitted game sets are a desirable later addition, but their
  research-purposes distribution grant and uncleared content copyright
  need a documented licensing determination before CI infrastructure
  depends on them; they are gated behind that review (Section 17). The
  StarCraft and Warcraft III sets are excluded outright: tess must not
  build on data hosted remove-upon-request. Other mirrored collections
  (GPPC, Iron Harvest, street maps) likewise require their own review
  before adoption.
- **Never vendored.** tess is MIT-licensed source; ODC-By game-derived
  data does not belong in its tree, and the sets are large. The cache
  location (Section 8.3) is outside the repository worktree specifically
  so that no `git add` can capture it.
- **Attribution.** When implemented, the mirror is recorded as an external
  data dependency in `docs/dependencies.md` (the one place downstream
  names are permitted), the fetch tool prints the ODC-By attribution and
  citation on first fetch, and any published page reporting numbers on
  this data (for example the performance page) carries the Sturtevant 2012
  citation plus the ODC-By notice and license link that the license
  requires of produced works.

## 7. Curated subset

The manifest (Section 8.1) selects individual files, not whole collections.
Selection criteria:

- All initial maps are exactly 512x512, drawn from `maze512`, `room512`,
  and `random512`. This matches the existing synthetic benchmark extent,
  satisfies the shape constraints with zero padding, and keeps the two
  harnesses directly comparable.
- Structural diversity over volume: on the order of 8-12 maps total —
  mazes at narrow and wide corridor widths, one or two room sizes, random
  fields at low and high obstacle density — each with its scenario file.
  Total download well under 20 MB.
- The game-derived sets are a later, licensing-gated addition
  (Section 6.3): `bg512` for realistic 512x512 layouts, then the Dragon
  Age sets (`dao`, `da2`, varied non-power-of-two sizes) to exercise the
  padding path (Section 9.3).

The exact file list is an implementation-time decision recorded in the
manifest; this TDD fixes only the criteria above.

## 8. Acquisition design

### 8.1 Manifest

A committed manifest file (under `tools/`) records:

- the upstream repository and pinned revision;
- for each file: its upstream relative path and SHA-256 checksum;
- the license identifier, attribution text, and citation.

The manifest is the single source of truth. Changing the pin or the file
list is an ordinary reviewed change with a visible diff.

### 8.2 Fetch tool

A Python tool under `tools/` (following the existing repo-tool conventions,
with pytest coverage) downloads each manifest entry from the mirror's raw
URL at the pinned revision, verifies its SHA-256, and installs it into the
cache. Checksum mismatch is a hard failure — it signals an upstream rewrite
and must never be silently accepted. Transient-failure retries follow the
same bounded-retry posture as the pinned git dependency population already
used by the build.

Installation is atomic per entry: each file downloads to a temporary path
and is renamed into place only after its checksum verifies, so concurrent
fetchers from different worktrees are safe by idempotent rename and a
partial download can never be mistaken for a cache hit. Consumers re-hash
cache entries against the manifest before use; a corrupted entry is
reported for refetch, never silently consumed.

### 8.3 Cache location

Default cache: `~/.cache/tess/benchmark-data/<upstream-revision>/...`
(respecting `XDG_CACHE_HOME`), overridable with `TESS_BENCHMARK_DATA_DIR`.
Rationale: the cache is shared across linked worktrees and build trees,
survives `build/` deletion, and cannot be committed. Revision-keyed paths
make pin bumps side-effect free.

### 8.4 Consumption and CI

- Data-dependent tests and benchmarks are opt-in behind a dedicated CMake
  option and locate data via the cache path; they do not fetch implicitly.
- When the data is absent the affected tests and benchmarks report an
  explicit skip with the fetch command; default presets are unaffected and
  perform no network access.
- CI runs the data-dependent suites in a dedicated job that restores the
  cache keyed on the manifest hash and fetches on miss. Default CI jobs do
  not depend on the data.
- The dedicated job runs in a strict required-data mode: a fetch failure,
  an incomplete manifest, or zero registered data-backed cases fails the
  job. Skips are a local-development affordance only; a job whose purpose
  is the data must never green-wash an accidental full skip.

## 9. Ingestion contract

### 9.1 Formats

`.map` and `.scen` follow the Moving AI specifications
(movingai.com/benchmarks/formats.html). A map declares `type octile`,
`height`, `width`, then row-major terrain characters, top row first. A
scenario line carries a bucket, the map name and dimensions, integer start
and goal coordinates, and the optimal path length as a decimal number.

The loader validates scenarios against the loaded map, not just
syntactically: the declared version must be one it supports, every row's
map name and dimensions must match the map actually loaded (the format
permits foreign maps and dimension-scaled rows; both are rejected), start
and goal must be in bounds and passable, and the optimum must be finite
and non-negative. Any violation is a hard load failure.

### 9.2 Terrain mapping

The loader maps terrain to a single `std::uint8_t` passability field:

- passable: `.` and `G`;
- blocked: `@`, `O`, and `T`;
- **any other byte is a hard load failure**, including `S` (swamp) and `W`
  (water), whose conditional semantics this mapping does not model, and
  any unknown character. Failing loudly guarantees the oracle's optima
  were computed under the same passability the world encodes.

The initial collections (Section 7) use only `.`, `@`, and `T`, so the
constraint costs nothing today and guards future additions.

### 9.3 Coordinates, shapes, and padding

- Scenario `x` is the column and `y` is the row of the top-first map grid;
  a cell maps to `Coord3{x, y, 0}` on a 2D shape of extent
  `{width, height, 1}`.
- The 512x512 sets load onto a compile-time shape with a benchmark-chosen
  chunk size; the existing suites' 512x512 shapes are the natural fit.
- Maps whose dimensions do not satisfy the shape constraints (later `dao`/
  `da2` additions) are padded: the world extent rounds each dimension up
  to the next valid size, padding tiles are blocked, and coordinates are
  preserved (no offset). Blocked padding cannot alter any shortest path or
  any scenario optimum.
- One world is loaded per map and shared across that map's scenarios.

## 10. Scenario oracle

### 10.1 Degraded orthogonal mode (no lattice dependency)

The published optimal lengths assume octile movement, so they do not apply
to 4-connected search. Without lattice Phase B the oracle therefore runs
in a degraded mode: for a deterministic sample of scenarios, an in-harness
reference search (uniform-cost Dijkstra, zero heuristic, no shared code
with the A* fast path beyond the world itself) computes the 4-connected
optimum, and `astar_path` must match it exactly — integer costs, no
tolerance — with `PathStatus::Found` agreement in both directions. This
already buys structured-map correctness; only the external-optimum
comparison waits.

### 10.2 Octile mode (requires lattice Phase B)

With `DiagonalSteps` available, each sampled scenario runs under
`DiagonalSteps<RequireBothClear>` with unit entry costs, and the oracle
asserts against the scenario's published optimum:

- The published lengths assume `sqrt(2)` diagonals and no corner cutting
  through blocked cells — the `RequireBothClear` rule. The oracle pins
  that rule; running it under `RequireOneClear` is a harness bug.
- tess reports fixed-point ticks at scale 128. With `L` the published
  optimum and `alpha = 181 / (128 * sqrt(2))`, approximately `0.9998932`,
  the oracle asserts that `ticks / 128` lies in `[alpha * L - eps,
  L + eps]`, where `eps` is a small absolute allowance for the scenario
  file's decimal rounding. Below the interval indicates an illegal edge
  or corner-cutting bug; above it, suboptimality or an inadmissible
  heuristic.
- Bound rationale: a diagonal costs `181/128 = 1.4140625` against
  geometric `sqrt(2)`, and face steps agree exactly, so any path's
  fixed-point cost sits between `alpha` and `1` times its `sqrt(2)` cost;
  the same interval therefore bounds the two metrics' optima. The window
  is asymmetric and scales with `L` — a symmetric relative tolerance
  would grow past real single-edge errors on long scenarios and hide
  bugs.
- The model window `(1 - alpha) * L` exceeds the smallest single-edge
  deviation (`sqrt(2) - 1`, about `0.414`) once `L` passes roughly
  `3.9e3`, so the external check alone cannot pin single-edge bugs on the
  longest maze scenarios. The octile mode therefore retains the Section
  10.1 exact-equality comparison against the in-harness zero-heuristic
  reference under the same fixed-point metric, which detects search bugs
  exactly at every length; the external interval check independently
  validates the cost model and corner rule.
- Every sampled scenario must be `Found`; an `Unreachable` or
  `Indeterminate` result against a solvable scenario is a failure.

### 10.3 Sampling

Scenario files carry on the order of thousands of entries per map. The
oracle samples deterministically (fixed stride per file, no randomness),
always including each file's largest-bucket scenarios, with the stride an
implementation-time knob sized to keep the opt-in suite within its CI
budget. The full-corpus run remains available locally via the same knob.

## 11. Chunked and sparse world adaptation

The maps adapt directly to the chunked and sparse machinery; every
adaptation below reuses the Section 8 acquisition and Section 9 loader
unchanged.

### 11.1 Chunk-size equivalence sweep

Chunk extent is a compile-time parameter of the loaded shape, so the same
map loads under several chunk sizes (for example 16, 32, and 64 on the
512x512 sets). Path results are chunk-layout invariant, so for each sampled
scenario the status and cost must be identical across the sweep and equal
to the Section 10 expectation. Game-map corridors cross seams at arbitrary
offsets and angles, which makes this the strongest available fixture for
seam-crossing expansion, portal enumeration, and boundary scans; synthetic
fixtures tend to align features to chunk boundaries and systematically
miss this class of bug.

### 11.2 Topology and precheck fixtures

The region graph and the reachability precheck get realistic portal and
region distributions — door-dense room maps and narrow-corridor mazes
immediately, game maps once adopted. The oracle extends naturally: the
precheck must never report a definitive unreachable result for a scenario
the search proves `Found`, evaluated per chunk size in the sweep above.

The room and narrow-maze sets are also natural bottleneck geometry for
the deferred flow, congestion, and crowd work in the roadmap; when those
designs are implemented, these fixtures are ready. Multi-agent contention
benchmarks themselves remain out of scope here (Section 5).

### 11.3 Sparse residency

Game maps are dominated by out-of-bounds `@` regions and the synthetic
sets by walls, so many chunks contain no passable tile and need never be
resident — real maps are natural sparse workloads. Adaptations:

- Load a map into a sparse world under a bounded residency budget and
  drive the existing stream-and-retry pattern (as in the `sparse_stream`
  example): search, materialize the chunks reported missing, retry.
- **Convergence oracle:** for every sampled scenario, the loop must
  terminate with status and cost equal to the dense-world result. While
  required chunks are non-resident, the result must be indeterminate — a
  wrong definitive answer under partial residency is a failure.
- Benchmarks: resident-chunk working set and retry count for long,
  structure-crossing scenarios, and behavior under narrow budgets.

## 12. Benchmark integration

- New benchmark entries run A* (and, with the lattice work, octile A*) on
  representative fetched maps: at minimum one narrow-corridor maze, one
  room map, and one random map — plus a game map once adopted — with
  fixed start/goal pairs drawn from the scenario data (long,
  structure-crossing instances).
- Entries register only when the data is present, live in the dedicated
  data-backed suite, and are gated by the same per-benchmark calibrated
  ceiling methodology as the existing suites, calibrated from the
  dedicated CI job's baseline artifacts.
- Existing synthetic suites are untouched and remain the default-path
  regression net.

## 13. Determinism and reproducibility

- The pinned revision plus per-file SHA-256 makes every fetch bit-exact;
  the cache is keyed by revision.
- Loader, sampler, and benchmark instance selection are deterministic;
  nothing derives from wall clock, environment, or map iteration order
  beyond the files themselves.
- A pin bump is a reviewed manifest change that re-verifies checksums and
  recalibrates any affected ceilings.

## 14. Tests

- Loader unit tests on small inline literal maps: terrain mapping, the
  hard-failure set (`S`, `W`, unknown bytes), coordinate orientation
  (asymmetric fixtures), padding behavior, and header validation.
- Manifest tests: schema, checksum format, and pinned-revision presence;
  fetch-tool tests cover checksum rejection and retry classification
  without network access (pytest, alongside the existing repo-tool tests).
- Oracle self-tests: the reference Dijkstra validated against known
  hand-computed fixtures; tolerance logic exercised at both boundaries.
- Skip-path tests: with an empty cache, the opt-in suites skip with the
  documented message and default suites are provably network-free.

## 15. Risks and mitigations

- **Upstream disappearance or rewrite.** The mirror could move or rewrite
  history. Mitigation: checksums make rewrites loud; the manifest isolates
  the URL scheme so a mirror migration is a one-file change; the curated
  subset is small enough to re-home quickly.
- **License drift.** Upstream terms could change. Mitigation: Section 6.2
  records the facts as of the pin date; a pin bump re-checks them as part
  of review.
- **CI flakiness from network fetch.** Mitigation: dedicated job, cache
  keyed on manifest hash, bounded retries, and no default-path dependency.
- **Oracle false confidence from sampling.** A sparse stride could miss a
  rare-instance bug. Mitigation: largest buckets always included;
  full-corpus runs available locally and before releases.
- **Divergence from the lattice implementation.** If shipped diagonal
  semantics deviate from the lattice TDD (costs, corner rule), oracle
  assertions become stale. Mitigation: Section 10.2 depends only on the
  normative constants and rule names; any change to those already requires
  a superseding design, which must revisit this oracle.

## 16. Alternatives rejected

### 16.1 Vendoring the data

Rejected. It mixes ODC-By game-derived data into an MIT source tree, bloats
the repository, and provides nothing the checksum-pinned fetch does not.

### 16.2 Fetching from movingai.com archives

Rejected as the primary source. The zip archives are unversioned and
mutable, offer no per-file pinning, and put load on a personal site. The
Shortest Path Lab mirror exists precisely to provide versioned,
attributable access.

### 16.3 Cloning the mirror

Rejected. The repository spans many collections and formats; a shallow or
sparse clone still couples tess to repository-level layout and size growth.
Per-file raw fetches at a pinned revision with checksums are simpler,
smaller, and equally reproducible.

### 16.4 Blizzard-derived sets

Rejected outright (Section 6.3): remove-upon-request hosting is an
unacceptable foundation for CI infrastructure, and the excluded sets add no
structural class the permitted sets lack.

### 16.5 Maintained solver-comparison benchmarks (HOG2, Warthog)

Rejected. HOG2 is a research testbed optimized for experimentation, not
throughput, so a comparison measures the wrong thing. Warthog is a tuned
static-map solver whose headline algorithms (JPS, preprocessing) answer a
different question from tess's dynamic-world substrate; a published
head-to-head misleads in both directions, and keeping an external
Bitbucket-hosted C++ build green in CI is recurring cost with no regression
signal. tess's regression model is calibrated self-relative ceilings. A
one-off offline calibration of plain A* against Warthog's baseline on
identical maps under identical octile costs is explicitly permitted and, if
performed, is recorded in the optimization log.

### 16.6 MAPF tracker participation

Rejected: coordinated multi-agent planning is outside the roadmap, and
submitting single-agent-derived results would misrepresent tess's scope.

### 16.7 Floating-point oracle comparison without tolerance

Rejected. The normative fixed-point model intentionally deviates from
`sqrt(2)` by about `1.07e-4` relative; exact comparison would fail
correct implementations, and an ad-hoc epsilon without the Section 10.2
derivation would either mask bugs or flake.

## 17. Implementation sequence

### Phase 1: acquisition and loader (independent of lattice work)

Manifest, fetch tool with checksum verification and attribution output,
cache layout, loader with the Section 9 contract, loader and tooling tests,
dependency documentation.

### Phase 2: degraded oracle, equivalence fixtures, and benchmarks

Reference-Dijkstra oracle on the initial subset, the chunk-size
equivalence sweep, topology precheck agreement, sparse stream-and-retry
convergence, structured-map benchmark entries, and the dedicated CI job
with cache and calibrated ceilings.

### Phase 3: octile oracle (once lattice Phase B is available)

Published-optimum assertions under `DiagonalSteps<RequireBothClear>` with
the Section 10.2 tolerance; extend benchmark entries with octile variants.

### Phase 4: game-derived sets (gated on licensing determination)

Document the licensing basis for the BioWare sets (Section 6.3), then add
`bg512` for realistic layouts and the `dao`/`da2` sets to exercise
non-512 extents and the padding rule.

## 18. Acceptance criteria

- The default presets build, test, and benchmark with no network access
  and no behavior change when the cache is empty.
- A single documented command fetches the curated subset, verifies every
  checksum, prints attribution, and populates the cache atomically; a
  corrupted or rewritten upstream file fails the fetch, and a corrupted
  cache entry is detected before consumption.
- Loader rejects every byte outside the supported terrain set and every
  malformed header, with tests.
- Degraded-mode oracle: A* agrees exactly with the reference search on
  every sampled scenario of the initial subset.
- Octile-mode oracle (with lattice Phase B): every sampled scenario is
  `Found` within the Section 10.2 tolerance of its published optimum,
  under `RequireBothClear` only.
- Chunk-size sweeps, precheck agreement, and sparse stream-and-retry
  convergence match the dense single-chunk-layout result on every sampled
  scenario.
- Structured-map benchmarks run under calibrated ceilings in a dedicated
  CI job whose data comes from the manifest-keyed cache, and that job
  fails rather than skips when the data or the registered cases are
  missing.
- `docs/dependencies.md` records the mirror, pin, license, and citation;
  published numbers on this data carry the citation.

## 19. Open questions

- The exact initial file list within the Section 7 criteria, fixed when
  the manifest lands.
- The sampling stride and CI time budget for the oracle suites.
- Whether the structured-map benchmark entries eventually feed the public
  performance page's trend snapshot, which would make the dedicated job's
  artifacts part of the published-data pipeline.

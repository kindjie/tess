# TDD: Lattices and the Resolved Transition Model

## 1. Status and relationship to earlier TDDs

This document defines the design for orthogonal diagonal movement and axial
hex worlds. It refines the historical design intent in:

- `core-shape-coordinate-key-system.md`
- `topology-and-region-graph.md`
- `pathfinding-core.md`
- `flow-distance-fields.md`
- `modern-cpp-compile-time-safety.md`

Where those documents conflict with this one, this document supersedes these
specific points:

- lattice is essential compile-time shape data in addition to size and chunk
  extent;
- pathfinding, topology, fields, and movement validation consume one resolved
  transition model, not independently implemented neighbor rules;
- `TransitionProvider` contributes exceptional, data-driven transitions to
  that model rather than defining the regular lattice stencil by itself;
- an inter-chunk portal may cross any chunk boundary permitted by the lattice,
  not only an axis-aligned face.

The TDD archive remains historical design intent. Maintained architecture
documents and the decision changelog become authoritative when implementation
lands.

## 2. Summary

World shape owns its coordinate lattice. Movement class owns the legal regular
step policy for a mover. A special transition provider contributes sparse,
data-driven edges such as stairs. The library resolves those inputs into one
compile-time transition model used by exact paths, topology, distance fields,
cache identity, and movement commits.

The first new lattices and step policies are:

- the existing orthogonal lattice with its existing face-adjacent default;
- clearance-preserving diagonal steps for orthogonal worlds with exactly two
  effective axes, requiring either both intermediate tiles or at least one
  intermediate tile to be clear and charging approximately `sqrt(2)` for a
  diagonal;
- a bounded axial hex lattice with six equal-cost planar neighbors.

The existing orthogonal API and behavior remain the default. There is no
runtime lattice switch and no virtual dispatch in hot loops.

## 3. Motivation

The current implementation encodes ordinary movement in several independent
places:

- A* and distance fields enumerate axis neighbors directly;
- topology flood fill and boundary exits implement their own axis rules;
- movement commits recognize adjacency through Manhattan distance;
- product invalidation widens dependencies only through chunk faces;
- special transition providers currently enrich the region graph but are not
  part of exact tile search.

Adding diagonal or hex movement independently to those sites would allow a
topology precheck to disagree with exact search, a cached product to omit an
affected chunk, or a path step to be rejected during commit. One shared model
is required for correctness.

## 4. Goals

- Preserve source compatibility for existing orthogonal worlds and movement
  classes through default template arguments and fallback traits.
- Preserve the current canonical `Coord3`, `TileKey`, chunk, and field-storage
  model for every lattice.
- Support configurable `RequireBothClear` and `RequireOneClear` diagonal
  movement in top-down and vertical 2D orthogonal worlds.
- Support bounded top-down axial hex worlds with rectangular axial bounds.
- Make topology, exact paths, fields, caches, and movement commits agree on
  legal transitions.
- Keep lattice and step selection compile-time and allocation-free in hot
  loops.
- Preserve deterministic transition ordering.
- Give diagonal steps a deterministic fixed-point approximation of geometric
  `sqrt(2)` cost without changing default orthogonal cost values.
- Expose compact-cost overflow risk without rejecting ordinary models whose
  worst-case bound is conservative or unavailable.
- Preserve dense and sparse residency semantics, including conservative
  `Indeterminate` results at missing transition targets.
- Keep exceptional transitions such as stairs composable with regular lattice
  movement.
- Expose capability traits so specialized algorithms remain possible.
- Preserve existing orthogonal code generation and performance within
  benchmark noise.

## 5. Non-goals

- Navigation meshes, mesh generation, arbitrary polygons, and continuous
  steering.
- Runtime-switchable lattice geometry within one world type.
- Unbounded or dynamically sized worlds.
- A visually hexagonal world boundary; axial bounds form a parallelogram.
- Separate hex storage, separate 2D storage, or a second key layout.
- Full 18-way or 26-way 3D diagonal movement in the first implementation.
- Exact irrational or floating-point diagonal step costs.
- A supported 64-bit cost-storage domain in the initial implementation; the
  internal contracts must leave room for a later separately tested domain.
- Unrestricted target-only diagonal corner cutting. Every built-in diagonal
  policy must require at least one clear intermediate tile.
- Making dynamic blockers part of persistent topology.
- Replacing game-defined render projection or presentation coordinates.

## 6. Ownership model

The design separates five concerns.

### 6.1 Shape lattice

The lattice defines:

- the semantic interpretation of canonical coordinates;
- the regular directions available to step policies;
- the default step policy;
- the admissible default distance function;
- regular cross-chunk relationships;
- which step policies are valid for the shape.

The initial lattice types are `lattice::Orthogonal` and
`lattice::HexAxial`.

### 6.2 Movement step policy

The step policy defines which regular lattice directions a movement class may
use, any static clearance rule, and geometric step-cost multipliers. It does
not own passability or destination entry cost.

The initial policies are:

- `movement::DefaultSteps`;
- `movement::DiagonalSteps<movement::CornerRule::RequireBothClear>`;
- `movement::DiagonalSteps<movement::CornerRule::RequireOneClear>`.

`RequireBothClear` is the default diagonal rule. The corner-rule vocabulary is
closed to these two values. There is intentionally no unrestricted `Allow`
rule.

`DefaultSteps` resolves to face adjacency on an orthogonal lattice and to six
planar neighbors on an axial hex lattice.

### 6.3 Movement class

The movement class continues to own passability and destination entry cost. It
also names the step policy. A raw passability tag still normalizes to
`WalkableField<Tag>` and therefore uses `DefaultSteps`.

### 6.4 Special transition provider

A provider contributes exceptional transitions whose presence depends on
world data, such as stairs, ladders, drops, or doors. Providers remain separate
from regular lattice stencils so common neighbors stay cheap and statically
enumerable.

### 6.5 Resolved transition model

The internal resolved model combines lattice, movement class, regular steps,
and special provider. Every algorithm that reasons about movement uses this
model or a proven connectivity projection supplied by it.

## 7. Public shape and coordinate API

The conceptual shape declaration becomes:

```cpp
template <Extent3 Size, Extent3 Chunk,
          typename Lattice = lattice::Orthogonal>
struct Shape;
```

Existing declarations remain source compatible:

```cpp
using Square = Shape<Extent3{128, 128, 1}, Extent3{16, 16, 1}>;
```

An axial hex world is explicit:

```cpp
using Hex = Shape<Extent3{128, 128, 1}, Extent3{16, 16, 1},
                  lattice::HexAxial>;
```

`ShapeTraits<Shape>` exposes `lattice_type` and a stable lattice identity.
Lattice identity is part of shape identity, derived-product identity, and save
metadata. Size, chunk counts, local indexing, and packed keys do not change.

### 7.1 Axial coordinates

Hex worlds use canonical coordinates as follows:

```text
Coord3.x = axial q
Coord3.y = axial r
Coord3.z = level
```

The initial hex lattice requires `size.z == 1` and `chunk.z == 1`.
`Box3` remains the universal bounds type and describes a parallelogram in
axial space. Renderers choose pointy-top or flat-top projection; projection is
not part of topology. `Coord3.z` is reserved for a future multi-level hex
design and is zero in the initial lattice.

A convenience type lowers without loss:

```cpp
struct HexCoord {
  std::int64_t q = 0;
  std::int64_t r = 0;
};

constexpr Coord3 to_coord3(HexCoord coord) noexcept;
constexpr HexCoord to_hex_coord(Coord3 coord) noexcept;
constexpr std::uint64_t hex_distance(HexCoord lhs,
                                     HexCoord rhs) noexcept;
```

`Coord3` remains authoritative in world, path, topology, dirty-bound, and
render-delta APIs.

## 8. Public movement API

The conceptual movement class gains a defaulted step-policy parameter:

```cpp
template <typename PassExpr, typename CostExpr,
          typename StepPolicy = DefaultSteps>
struct MovementClass;
```

Existing two-argument uses remain source compatible. Manually authored
movement classes that do not name `step_policy` resolve through
`step_policy_of<Class>` to `DefaultSteps`; `MovementClassFor` does not become
source-incompatibly stricter.

Example diagonal class:

```cpp
using Walker8 = movement::MovementClass<
    movement::Field<PassableTag>, movement::UnitCost,
    movement::DiagonalSteps<
        movement::CornerRule::RequireBothClear>>;
```

Example hex class:

```cpp
using HexWalker = movement::MovementClass<
    movement::Field<PassableTag>, movement::UnitCost>;
```

The hex lattice's default provides its six neighbors, so the class need not
repeat world geometry.

Existing path, topology, runtime, and movement function signatures infer the
resolved step policy through `World::shape_type` and the movement class.

Path costs carry model ticks and their scale. `PathResult::cost` and
`NearestTargetResult::cost` remain `std::uint32_t`. Both result types append a
`std::uint32_t cost_scale = 1` member. Default orthogonal and hex results retain
scale one and the same numeric cost as the current API. Diagonal results use
scale 128. A caller interprets a result as the mathematical ratio
`cost / cost_scale` in movement-class entry-cost units and compares costs only
after normalizing their scales.

The appended defaulted member preserves existing aggregate initialization.
Public helpers use `std::uint64_t` intermediates for overflow-safe comparison
and provide floating-point conversion for presentation; search itself uses
integers.

The public compile-time range assessment is:

```cpp
enum class CostRangeAssessment : std::uint8_t {
  ProvenSafe,
  PotentialOverflow,
  Unknown,
};

template <typename World, typename MovementClass,
          typename Provider = EmptyTransitionProvider>
inline constexpr CostRangeAssessment path_cost_range_assessment = /* ... */;
```

`ProvenSafe` means a conservative bound fits below the reserved infinite-cost
sentinel. `PotentialOverflow` means all required bounds are known and their
product exceeds that limit; it does not predict that a particular query will
overflow. `Unknown` means a cost expression or provider does not advertise a
usable maximum. The trait is documented beside every path family and is also
available from typed runtimes.

The default API does not warn or reject any assessment. This avoids noisy
template-instantiation warnings and warnings-as-errors failures. Applications
that require a proof may invoke an opt-in
`require_proven_path_cost_range<World, MovementClass, Provider>()` consteval
helper, which issues a focused `static_assert` unless the result is
`ProvenSafe`.

## 9. Regular transition semantics

### 9.1 Orthogonal default

`DefaultSteps` on `lattice::Orthogonal` retains the current fixed order:

```text
+x, -x, +y, -y, +z, -z
```

Degenerate axes contribute no candidates. Transition cost is the destination
tile's positive entry cost, preserving current unit and weighted semantics.

### 9.2 Clearance-preserving diagonals

`DiagonalSteps<CornerRule>` is valid only on `lattice::Orthogonal` when exactly
two shape axes are effective and `CornerRule` is `RequireBothClear` or
`RequireOneClear`. It emits the existing face directions first, followed by
the four two-axis diagonals. Let `a` and `b` be the effective axes in `x`, `y`,
`z` order; diagonal order is `(+a, +b)`, `(+a, -b)`, `(-a, +b)`, then
`(-a, -b)`.

A diagonal is legal only when:

- its target is in bounds and passable for the movement class; and
- under `RequireBothClear`, both face-adjacent intermediate tiles are in
  bounds, resident, and passable for the same movement class; or
- under `RequireOneClear`, at least one face-adjacent intermediate tile is in
  bounds, resident, and passable for the same movement class.

An out-of-bounds or resident impassable intermediate is determinately blocked.
A non-resident intermediate is `MissingTopology` when its state is needed to
decide the edge. Under `RequireBothClear`, either blocked intermediate blocks
the edge, while one clear and one missing intermediate is indeterminate. Under
`RequireOneClear`, either clear intermediate makes the edge legal, both blocked
intermediates block it, and a missing intermediate with no known-clear
alternative is indeterminate.

Dynamic occupancy and reservation remain final-target concerns unless a
future API carries the same blocker policy through both planning and commit.
The initial implementation checks clearance tiles against static
movement-class passability only. Persistent topology uses the same static
passability.

Diagonal-enabled models use fixed-point cost scale 128. A face step costs the
destination entry cost multiplied by 128 ticks. A diagonal costs the
destination entry cost multiplied by 181 ticks. Thus a diagonal has geometric
factor `181 / 128 == 1.4140625`, approximately `sqrt(2)` with about 0.0107%
relative error. The constants are normative and form part of step-policy and
cache identity.

Unit-entry-cost diagonal A* uses the matching integer octile heuristic. For
effective-axis deltas `major >= minor`, its lower bound is:

```text
minor * 181 + (major - minor) * 128
```

Weighted search multiplies each term by the minimum legal destination entry
cost. Edge multiplication and accumulated `g` addition are checked in the
existing `std::uint32_t` cost domain; an unrepresentable `g` is skipped and
recorded as described in Section 10.4. Heuristic and `f` arithmetic saturate,
but an `f`-saturated node remains in the frontier and is ordered by the normal
stable secondary keys. The largest finite diagonal-model result represents
approximately 33.5 million unscaled entry-cost units. This is a deliberate
range tradeoff for preserving the public type and compact field storage. No
floating-point operation participates in ordering, equality, reconstruction,
or cache identity.

Both diagonal rules preserve reachability components: every legal diagonal
has a passable two-step face path through at least one clear intermediate. The
resolved model advertises this through
`preserves_default_connectivity == true`.

Unrestricted target-only corner cutting is rejected, not deferred. It would
permit transitions between distinct face-connected components, make touching
obstacles permeable, and require full diagonal topology, corner portals, and
wider invalidation. Those semantics are outside the roadmap. Any future
proposal to introduce them must supersede this decision in a new TDD rather
than extending `CornerRule`.

### 9.3 Axial hex default

The axial hex lattice emits six directions in a documented fixed order:

```text
(+1,  0), (-1,  0), ( 0, +1), ( 0, -1), (+1, -1), (-1, +1)
```

Every step charges the destination entry cost exactly once. Unit A* uses
overflow-safe axial hex distance:

```text
(abs(dq) + abs(dr) + abs(dq + dr)) / 2
```

The implementation must compute the expression without signed overflow.
Hex adjacency defines its own connected components and cannot project to
orthogonal face connectivity.

## 10. Internal resolved-transition contract

The exact spelling is implementation-defined, but the resolved model must
provide the equivalent of:

```cpp
template <typename Model, typename World>
concept ForwardTransitionModelFor = requires(
    const Model& model, const World& world, Coord3 from,
    std::uint64_t from_index, Coord3 goal, TransitionSink sink,
    ChunkSink chunk_sink) {
  model.for_each_forward(world, from, from_index, sink);
  model.for_each_dependency_chunk(world, from, chunk_sink);
  model.heuristic(world, from, goal);
  model.revision();
};

template <typename Model, typename World>
concept ReverseTransitionModelFor =
    ForwardTransitionModelFor<Model, World> &&
    requires(const Model& model, const World& world, Coord3 to,
             std::uint64_t to_index, TransitionSink sink) {
      model.for_each_reverse(world, to, to_index, sink);
    };
```

Candidate probing is three-valued so sparse callers can distinguish an illegal
edge from one whose legality depends on missing topology:

```cpp
enum class TransitionAvailability : std::uint8_t {
  Legal,
  Blocked,
  MissingTopology,
};
```

Each probe contains enough information for hot consumers without re-resolving
it:

```cpp
template <typename Cost = std::uint32_t>
struct TransitionProbe {
  Coord3 to{};
  std::uint64_t to_index = 0;
  Cost cost = 0;
  TransitionKind kind = TransitionKind::Regular;
  TransitionAvailability availability = TransitionAvailability::Blocked;
};
```

The real contract may use callbacks, traits, or split fast-path functions to
avoid materialization. It must not allocate a neighbor list or use virtual
dispatch. `TransitionKind` distinguishes at least `Regular` and `Special`.
Per-edge and accumulated costs retain the existing saturating `std::uint32_t`
domain in the initial implementation. Algorithms name the model's `cost_type`
rather than hard-code it so a later wide-cost TDD does not require another
transition-contract redesign. The model exposes its positive `cost_scale`,
which is one for default orthogonal and hex movement and 128 for
diagonal-enabled movement.

Hot enumeration sends `Legal` and `MissingTopology` probes to the sink.
`Blocked` is available to checked validation and diagnostics but need not
cross the hot callback boundary. Search records missing probes separately and
never inserts them into the frontier.

### 10.1 Capability traits

The model exposes compile-time capabilities including:

- maximum regular degree;
- cost type;
- whether all regular costs are unit costs;
- cost scale and fixed regular-step multipliers;
- whether regular transitions are symmetric;
- whether the heuristic is consistent;
- whether regular connectivity equals default lattice connectivity;
- whether reverse enumeration is available;
- chunk dependency radius or exact dependency-offset enumeration;
- whether special transitions exist;
- whether a specialized open set or direct-path probe is valid.

Algorithms select optimized implementations with `if constexpr`. Unknown or
custom models receive correct conservative fallbacks.

### 10.2 Forward and reverse consistency

For every directed transition `a -> b`, forward enumeration at `a` and reverse
enumeration at `b` must expose the same edge and cost semantics. Symmetric
regular lattices may derive reverse enumeration from the same stencil.
With destination entry costs, reverse enumeration of `a -> b` charges the
entry cost of `b`, matching forward search rather than the predecessor's cost.
Reverse enumeration emits a resident predecessor only when it is passable and
forward enumeration from that predecessor would emit the transition. A
decision-relevant missing predecessor or clearance tile is reported as
missing topology rather than emitted as legal.

Special providers express costs in unscaled movement-class entry-cost units.
The resolved model multiplies them by its cardinal-step multiplier before
emitting a probe. Consequently a stair composed with diagonal movement costs a
cardinal step, and provider authors do not encode or duplicate model scale.

A special provider that cannot enumerate reverse edges is usable by forward
exact search but is not eligible for reverse fields. This is a compile-time
diagnostic, not silent omission.

### 10.3 Special providers

The current chunk-oriented provider API remains valid for topology builds.
Exact search additionally requires allocation-free per-origin forward
enumeration. Reverse fields additionally require per-target reverse
enumeration. Separate concepts provide focused diagnostics for those
capabilities. The built-in empty and stair providers gain both operations.
Custom chunk-only providers remain source compatible for topology but must add
the new operations before callers can pass them to exact search or fields.

Provider transitions derived from world fields read current data during
per-origin enumeration. Graphs and cached products capture the existing chunk
topology versions, provider revision, and residency dependencies, so a world
edit cannot leave provider-derived edges fresh accidentally.

The initial implementation does not build a generic provider index. This
avoids unresolved ownership, lifetime, and invalidation costs. A future
library adapter may build a versioned per-origin index, but it requires its own
design and benchmarks.

Stateful provider revision remains part of the resolved model identity.

A resolved model containing special transitions disables regular
direct-geodesic short-circuits unless the provider advertises and the probe
uses a bound proving that no special route can undercut the direct result.

### 10.4 Compact-cost range assessment

Cost expressions and providers may advertise a positive compile-time maximum
transition cost in unscaled entry-cost units. When all maxima are known, the
model computes a conservative bound with the repository's widened constexpr
integer support:

```text
(maximum simple-path edges) * (maximum transition cost) *
    (maximum step multiplier)
```

Positive edges guarantee that an optimal coordinate-only path can be simple,
so a dense bounded world uses at most `tile_count - 1` edges. An
algorithm-specific smaller bound may be used only when proven. Sparse
worlds conservatively use the full shape tile count unless their type exposes
a smaller compile-time residency bound. A cost expression or provider without
a sound advertised maximum produces `Unknown`; runtime query bounds do not
participate in the static result. The calculation itself must not overflow.

The largest finite compact tick value is one below the existing
`std::uint32_t` infinite-cost sentinel. If edge-cost multiplication or a
relaxation exceeds it, the search records that it skipped a cost-overflow
transition. A representable `Found` result remains valid because a positive
overflowed route cannot undercut it. On exhaustion, result precedence is
`Indeterminate`, then `CostOverflow`, then `NoPath`. Under `TreatAsBlocked`,
`CostOverflow` precedes `NoPath`.

`PathStatus::CostOverflow` carries no path or usable cost. Field and cached
product builders return it rather than publishing a product that marks
overflow-reachable nodes as unreachable. Batch APIs report it per request.
Overflow products are not cached as successful results. This runtime status
makes realized risk visible without obstructing models whose conservative
assessment is merely `PotentialOverflow` or `Unknown`.

A search or bounded field with a separately proven runtime-domain cost bound
below the finite limit cannot report `CostOverflow`, even when the whole-model
static assessment is conservative. Implementations may compile out overflow
bookkeeping with `if constexpr` when the model is `ProvenSafe`; checked
arithmetic remains required for other assessments.

## 11. Pathfinding behavior

Every exact path entry point, including unit, weighted, cached, and batch
search, resolves one model from world shape, movement class, and special
provider. Validation, expansion, heuristic selection, and reconstruction use
that model.

Provider-aware overloads accept a trailing provider and default to the empty
provider. The same provider instance and identity flow through optional
topology prechecks, runtimes, caches, and movement commits. A graph built for a
different resolved model is conservatively bypassed rather than trusted.

The existing orthogonal face-adjacent specialization remains intact. New
models initially use the general heap implementation unless a capability
selects a proven specialization.

Axis-specific optimizations are enabled only for models whose traits and
proofs permit them:

- direct Manhattan axis orders;
- full-axis barrier rejection;
- parallel detours;
- plane-gap and forced-gap scans;
- the two-bucket unit open set;
- axis-order chunk portal candidates.

The existing two-bucket set relies on Manhattan parity: a unit face step
changes Manhattan distance by exactly one. Fixed-point octile and hex distance
do not share that exact property for every candidate, so they use another
proven queue until a model-specific bucket proof exists.

Diagonal and hex models require their own verified direct-geodesic probes.
Disabling an optimization must never change status, optimal cost, transition
ordering guarantees, or sparse missing-chunk semantics.

If a search skips a legal transition target because a required target or
clearance chunk is non-resident, exhaustion reports `Indeterminate` under the
existing policy. `TreatAsBlocked` retains its current behavior.

A successful sparse search is optimal over the resident subgraph visible to
that query, matching existing sparse semantics. It is not a promise of the
path that would be optimal after missing topology becomes resident. For
example, a missing required-clearance tile can hide a cheaper diagonal while
a resident face route still succeeds.

Weighted built-in models multiply their lattice distance in model ticks by the
minimum legal entry cost, which is one for the current normalized positive-cost
vocabulary. Diagonal models use octile distance with the same 128 and 181
multipliers as their edges. A model or provider that can introduce a cheaper
long-range transition must advertise a safe lower bound and compatible
heuristic; otherwise the resolved model uses a zero heuristic. Heuristic
arithmetic saturates through the public `std::uint32_t` path-cost range.

The initial stair provider does not advertise a lattice-heuristic bound,
because one stair can make more lattice progress than its entry cost. Searches
that explicitly compose stairs therefore use the zero-heuristic fallback until
a separately proven bound is introduced.

## 12. Distance and flow fields

Forward path reconstruction and reverse field construction use the same
resolved model. Reverse BFS remains available only when every regular and
special transition cost is one. Weighted and non-unit models use reverse
Dijkstra or a bounded-cost queue when the complete model advertises a safe
bound. Diagonal movement is non-unit even when every entry cost is one, so it
does not use the existing reverse BFS.

Field-product identity includes:

- lattice identity;
- normalized movement-class identity;
- normalized step-policy identity;
- cost-model identity;
- special-provider type and revision;
- existing world, goal, topology, residency, and bounds dependencies.

A product must not replay through a model with a different identity.

Dependency capture asks the model which chunks can affect any reached
transition. It must not assume face-only movement. Both diagonal corner rules
may use their default-connectivity projection when that produces an equivalent
conservative dependency set. A legal diagonal makes at least one clearance
tile face-reachable. A target in a corner-neighbor chunk is therefore a face
neighbor of a reached clearance tile's chunk. If clearance is blocked, an edit
to the corner target alone cannot legalize the diagonal. Widening dependencies
from every reached chunk through face neighbors is consequently conservative.

## 13. Topology and portals

Local topology uses either:

- the resolved model's full static regular connectivity; or
- a proven connectivity projection advertised by the model.

Both diagonal corner rules use orthogonal face components and the existing
face portals because their diagonals do not change reachability. Hex movement
uses all six axial directions and explicit cross-chunk targets.

Boundary records must identify the exact transition target when it cannot be
derived. Existing `LocalBoundaryExit` layout and entries remain unchanged for
orthogonal default and clearance-preserving diagonal topology. A separate
model-dependent boundary-transition record stores explicit source and target
coordinates for non-face transitions such as axial hex `(+1, -1)`. Default
orthogonal products do not populate per-entry lattice storage.

Hex topology uses the explicit boundary-transition record for all six
cross-chunk directions, including its four axis-aligned directions, so one
hex build has one boundary representation and ordering rule. Its diagnostic
dominant face uses the largest coordinate delta, breaking ties in `x`, then
`y`, then `z` order.

`BoundaryFace` remains a diagnostic dominant-axis label. Endpoint coordinates
are authoritative for non-face transitions. Boundary records are derived
products and are not part of saved-world serialization.

Incremental updates widen dirty chunks through model-provided dependency
offsets rather than a hard-coded face-neighbor list. Fresh and incremental
builds must remain byte-equivalent in region, portal, and adjacency order.

On sparse worlds, any resident region with a potentially legal transition
into required missing topology is marked as reaching missing topology.
Reachability remains conservative and never converts uncertainty into a
definitive `Unreachable`.

Region graphs stamp lattice, movement class, step policy, provider type, and
provider revision. A mismatch forces a full rebuild or a conservative bypass;
it is never accepted as fresh.

The default-connectivity projection is valid only for boolean reachability.
It must not supply path-cost bounds or heuristics for diagonal models because
face routes can be more expensive than diagonal routes.

## 14. Movement validation and agents

`validate_movement_intent` and `commit_movement_intent` replace the current
Manhattan-distance adjacency test with resolved-model validation. Existing
validation already checks bounds, residency, endpoint passability, occupancy,
reservation, and versions; the default orthogonal model preserves those
checks. New models add static clearance and explicitly supplied special-edge
rules. Dynamic occupancy and reservation remain target-only in the initial
design.

Plan and commit retain the invariant:

```text
every static transition accepted by a path for class C is accepted by
movement validation for class C when versions and dynamic occupancy have not
changed
```

Path agents bind runtime caches and commits to the same normalized transition
identity. Rebinding to another lattice-incompatible or step-incompatible class
clears affected caches through the existing class-binding mechanism.

Provider-aware movement overloads require the same provider identity used by
planning. Supplying a provider can make a non-face special step commit-legal;
omitting it preserves existing face-only behavior.

A missing diagonal clearance chunk returns the existing `StaleVersion` status
when its state is required to decide legality, matching a missing endpoint.
Under `RequireOneClear`, a known clear intermediate makes the other
intermediate irrelevant and does not fail validation. "Dynamic occupancy" in
the plan/commit invariant includes reservation state.

## 15. Determinism

Each built-in lattice and step policy defines a stable candidate order.
Providers preserve their existing deterministic-enumeration requirement.
The resolved model defines whether regular or special transitions appear
first; the initial rule is regular transitions followed by special
transitions.

Fresh builds, incremental builds, serial searches, batched searches, and field
reconstruction use the same order. Tie-breaking remains stable by score, path
cost, and tile identity after transition order has been applied.

## 16. Performance requirements

- Existing orthogonal face movement must remain benchmark-equivalent and
  should remain code-generation-equivalent in its hot loop.
- Built-in regular stencils use compile-time dispatch with no function-pointer
  or virtual call per candidate.
- No neighbor list allocates or materializes a dynamic container.
- Indexed neighbor generation retains the current fast chunk/local arithmetic;
  new policies must not regress to repeated global coordinate-to-key lookup in
  the steady-state hot loop.
- Degenerate axes compile out.
- Diagonal enumeration may derive legality for either corner rule from a
  compact ephemeral clear/blocked/missing mask so intermediate tiles are not
  reread unnecessarily. The initial mask is rebuilt per expansion and is not
  a cached derived product.
- The generic transition abstraction must not force all models onto one open
  set, heuristic, or topology representation.
- Diagonal frontiers, fields, products, and segment caches retain compact
  32-bit cost storage. Benchmarks include saturation-boundary fixtures so the
  range tradeoff cannot become implicit.
- Compile time, object size, and instruction-cache growth are benchmarked for
  representative world/class/model combinations.

Expected regular candidate degrees are four for top-down orthogonal face
movement, eight for clearance-preserving top-down diagonals, and six for axial
hex. Increased per-node work may be offset by shorter paths and stronger
geometry-specific heuristics; no performance claim is accepted without this
repository's benchmarks.

## 17. Compatibility and persistence

### 17.1 Source compatibility

- `Shape<Size, Chunk>` remains orthogonal.
- `MovementClass<PassExpr, CostExpr>` retains default steps.
- raw passability tags retain orthogonal face behavior on existing shapes.
- existing path, topology, field, and movement calls that omit a provider
  retain empty-provider behavior.
- custom movement classes without a step-policy member retain default steps.

The library is header-only and pre-1.0; adding defaulted template parameters
still changes type identity and requires recompilation. User forward
declarations and template-template parameters that reproduce the old template
arity require source changes.

`PathResult` and `NearestTargetResult` append a defaulted `cost_scale` member.
Their existing `std::uint32_t cost` member, existing member access, and
aggregate initialization remain valid. Default orthogonal numeric values do
not change. Diagonal callers that assumed every new model would use scale one
must account for the reported scale.

Structured bindings that name every result member must add `cost_scale`.
Exhaustive switches over `PathStatus` must handle `CostOverflow`. These are
documented source migrations even though ordinary member access and
non-exhaustive aggregate initialization remain compatible.

### 17.2 Deliberate behavioral changes

Existing orthogonal face-only calls with no provider retain their observable
path, topology, field, and movement behavior. Provider-aware exact-search,
field, and movement overloads are new. The one deliberate exception is a cost
sum that reaches the reserved infinite-cost sentinel: it reports the new
`CostOverflow` status rather than becoming indistinguishable from `NoPath`.

Existing orthogonal topology builds that explicitly supply a provider also
retain their local labels, boundary exits, portal order, CSR adjacency order,
and reachability answers during the Phase A refactor. The resolved model emits
regular portals followed by provider portals in the existing canonical
per-chunk order. Any future change to that ordering requires a separate
compatibility decision and changelog entry.

When a provider is explicitly supplied, Phase D makes its legal edges visible
to exact search, reverse fields, and movement commits. Results may therefore
be shorter, change from `NoPath` to `Found`, or accept a non-face movement
commit that the current implementation rejects. This deliberate change fixes
the historical disagreement in which provider edges could make topology
reachable while exact search could not traverse them. Release notes provide
migration guidance for provider authors and callers.

### 17.3 Saved data

Tile-key packing does not change, but lattice changes movement semantics. Save
metadata records a stable lattice identifier and version. Loading identical
size/chunk/key metadata under a different lattice requires explicit migration
or rejection. Derived topology, path caches, and fields are invalidated rather
than migrated. Adding lattice and step-policy identity causes a one-time
invalidation of persisted derived products on upgrade.
Persisted derived products also stamp cost scale and step multipliers; products
created before fixed-point diagonal costs are invalidated rather than
reinterpreted.

## 18. Implementation sequence

### Phase A: transition-model foundation

- Add orthogonal lattice and default-step vocabulary around existing behavior.
- Add lattice and step-policy identity/stamping.
- Route existing path, field, topology, and movement adjacency through the
  resolved model while retaining the current orthogonal specialization.
- Route unit, weighted, cached, and batch search through the same model.
- Establish forward/reverse and dependency contracts.
- Add model cost-type vocabulary, non-blocking range assessment, and explicit
  runtime overflow propagation without changing representable results.
- Pin source compatibility, allocation behavior, and benchmark parity.

### Phase B: clearance-preserving diagonals

- Add both corner rules, fixed-point step costs, and the integer octile
  heuristic for the 2D-effective-axis diagonal policy.
- Reuse default connectivity for topology.
- Add diagonal exact search, fields, validation, sparse behavior, caching,
  policy-specific invalidation, and direct-geodesic optimization.

### Phase C: axial hex lattice

- Add lattice traits, coordinate helpers, six-neighbor indexed enumeration,
  and overflow-safe hex distance.
- Add hex local topology, cross-chunk portals, dirty widening, fields,
  validation, sparse behavior, and caching.
- Add a hex direct-geodesic optimization after correctness baselines.

### Phase D: special-transition completion

- Adapt built-in special providers to per-origin forward and reverse
  enumeration.
- Add provider-aware trailing overloads for exact paths, batches, fields,
  runtimes, path agents, and movement validation/commit.
- Make exact paths and reverse fields consume the same special edges as
  topology.
- Preserve provider revision and sparse missing-topology behavior.

## 19. Tests

### 19.1 Compile-time and API tests

- Existing two-argument shapes and movement classes compile unchanged.
- Forward declarations and template-template uses receive documented migration
  examples.
- Lattice and step-policy concepts reject invalid combinations clearly.
- Hex rejects non-degenerate z in the initial implementation.
- Diagonal steps reject shapes without exactly two effective axes.
- Axial hex rejects `DiagonalSteps` even though it has two effective axes.
- Diagonal corner rules accept only `RequireBothClear` and `RequireOneClear`.
- Result-cost aggregate initialization remains valid with the appended
  default-one scale.
- Unit, bounded-cost, provider-bounded, and unknown models expose the expected
  non-blocking `CostRangeAssessment`.
- The opt-in strict range helper accepts only `ProvenSafe` models with a
  focused diagnostic for the other assessments.
- Custom legacy movement classes fall back to default steps.
- Unsupported reverse providers fail with a focused diagnostic.

### 19.2 Correctness tests

- Existing orthogonal face-only, empty-provider path, topology, field, and
  movement tests remain behavior compatible where observable.
- Diagonal open paths, obstacles, both corner rules, weighted entry costs,
  deterministic ties, vertical 2D, and path-to-commit agreement.
- Diagonal and hex unit, weighted, cached, and batch searches agree with their
  single-request references.
- Diagonal face and diagonal edges cost exactly 128 and 181 ticks per unit
  destination entry cost, and results report scale 128.
- Integer octile heuristics are admissible and consistent against a
  zero-heuristic reference, including weighted and saturating extremes.
- Representable paths remain `Found` after more expensive overflow candidates
  are skipped; exhaustion follows the documented `Indeterminate`,
  `CostOverflow`, and `NoPath` precedence.
- One fixture combines decision-relevant missing topology and overflow skips
  to pin their precedence end to end.
- Field, batch, cache, runtime, and nearest-target APIs propagate
  `CostOverflow` without publishing an incomplete successful product.
- Proof fixtures showing both diagonal rules preserve default connected
  regions.
- Hex neighbors, distances, shortest paths, weighted paths, boundaries,
  chunk-corner transitions, and deterministic ties.
- Distance-field reconstruction exactly matches forward transition legality.
- Topology prechecks never reject a route available to exact search.
- Fresh and incremental region graphs are identical for each model.
- Cache and product reuse reject lattice, step, cost, or provider mismatches.
- Hex coordinate conversion round-trips and hex-distance metric properties are
  checked at normal and signed-extreme coordinates.
- Built-in provider edges compose correctly with orthogonal, diagonal, and hex
  regular transitions in forward paths, reverse fields, and commits.
- Existing provider-supplied orthogonal topology builds retain labels, exits,
  portals, CSR adjacency, and reachability output through Phase A.

### 19.3 Sparse and invalidation tests

- Missing targets and clearance chunks produce the correct blocked or
  `Indeterminate` status.
- A missing diagonal clearance tile can hide a cheaper diagonal while a
  resident face route succeeds with resident-subgraph optimality.
- `RequireOneClear` is determinate when one intermediate is clear, even if the
  other is missing, and is indeterminate when a missing intermediate could be
  the sole clear route.
- Hex transitions into face- and corner-neighbor chunks mark missing topology.
- Dirty widening covers every chunk whose transition legality can change.
- A cached diagonal path is invalidated when an edit changes a clearance tile
  in a chunk containing none of the path's returned nodes.
- Residency eviction/reload invalidates model-dependent products.

### 19.4 Performance-contract tests

- Warm path, field, topology, and movement operations remain allocation-free.
- Existing orthogonal neighbor order and expansion counts remain pinned.
- Existing expansion-count pins use empty-provider configurations; provider
  overloads have separate expected behavior.
- No provider performs a whole-chunk scan per expanded node.
- Default orthogonal products do not gain per-entry lattice storage.
- Default orthogonal returned costs retain their prior numeric values and
  report scale one.
- Axial hex returned costs report scale one.
- Compile-time range assessment adds no hot-loop branch or storage.
- `ProvenSafe` instantiations compile out overflow bookkeeping.

## 20. Benchmarks

- Existing orthogonal open, maze, no-path, weighted, batch, field, topology,
  sparse, and agent benchmarks as non-regression baselines.
- Diagonal open geodesic, corner-heavy maze, no-path flood, both clearance
  masks, weighted search, batch search, field build, and movement commit.
- Hex open geodesic, maze, no-path flood, weighted search, field build,
  batch search, topology rebuild, incremental seam edit, and sparse boundary.
- Transition enumeration within a chunk and across each relevant seam.
- Provider-heavy exact and reverse searches proving per-origin enumeration
  avoids a whole-chunk scan per expanded node.
- Compile-time and binary-size comparison for one and many movement classes.
- Scratch, frontier, field, and cache memory per reached tile for default,
  diagonal, and hex models, including confirmation that compact diagonal costs
  do not widen per-tile cost storage.

## 21. Alternatives rejected

### 21.1 Movement-class-only hex geometry

Rejected because the same world could be interpreted as square by one
subsystem and hexagonal by another, and because coordinate distance, boundary
semantics, persistence, and rendering diagnostics are world-level facts.

### 21.2 Shape-only diagonal geometry

Rejected because different movement classes in one orthogonal world may need
face-only or diagonal steps.

### 21.3 Per-query runtime policy

Rejected because it adds hot branches and makes callers responsible for
keeping path, topology, field, cache, and commit policies identical.

### 21.4 Universal runtime transition callback

Rejected because virtual or type-erased per-neighbor dispatch obstructs
inlining, indexed-neighbor arithmetic, static degree bounds, and specialized
open sets.

### 21.5 Separate diagonal and hex path families

Rejected because duplicated algorithms would drift from topology, fields,
sparse semantics, caches, and movement validation.

### 21.6 Unrestricted diagonal corner cutting

Rejected because a target-only diagonal can connect otherwise disconnected
face components, pass between touching obstacles, and disagree with the
physical clearance expected by non-point agents. Supporting it would also
forfeit the face-connectivity projection and require diagonal local topology,
corner portals, and wider dependency invalidation. The roadmap includes only
`RequireBothClear` and `RequireOneClear`.

### 21.7 Unit-cost and floating-point diagonals

Unit-cost diagonals distort geometric distance and movement range. Exact
floating-point `sqrt(2)` costs introduce platform-sensitive ordering and cache
semantics. The fixed 181/128 ratio is deterministic, accurate enough for
path choice, and admits integer heuristics and equality checks.

## 22. Risks and mitigations

- **Branching-factor cost:** retain specialized built-in stencils, add direct
  geodesic probes, and profile before adding more fast paths.
- **Template multiplication:** normalize policy identities, factor cold code,
  and measure compile time and binary size.
- **Topology growth:** use the face-connectivity projection proven for both
  supported diagonal corner rules.
- **Sparse uncertainty growth:** make dependency enumeration model-provided and
  preserve conservative `Indeterminate` behavior.
- **Provider hot-loop cost:** require per-origin enumeration and prohibit
  whole-chunk scans during node expansion.
- **Heuristic mistakes:** require admissibility/consistency tests against
  zero-heuristic reference searches.
- **Fixed-point range reduction:** document the finite Q7 range, retain
  overflow-status tests, expose a non-blocking static assessment, and require a
  new cost-model design if real workloads need more than approximately 33.5
  million unscaled cost units.
- **Persistence ambiguity:** stamp lattice identity even though key packing is
  unchanged.

## 23. Acceptance criteria

- Existing orthogonal face-only, empty-provider source and observable behavior
  remain compatible, except documented template-arity, structured-binding,
  exhaustive-status-switch, and overflow-status migration cases.
- Existing orthogonal performance remains within calibrated benchmark noise.
- Existing provider-supplied orthogonal topology output remains canonically
  ordered and observably compatible through the foundation refactor.
- Shape identity includes a compile-time lattice with orthogonal as default.
- Movement-class identity includes a defaulted regular step policy.
- All movement consumers resolve the same transition model and identity, or a
  connectivity projection proven equivalent for their query.
- Both diagonal corner rules work in every two-effective-axis orthogonal
  orientation supported by the existing shape model.
- The diagonal corner-rule API exposes no unrestricted `Allow` value, and no
  roadmap phase depends on target-only corner cutting.
- Diagonal face and diagonal costs use the normative 128 and 181 tick
  multipliers, with an admissible integer octile heuristic and existing 32-bit
  saturation semantics.
- Public path and nearest-target results report both model ticks and their
  scale; existing orthogonal calls retain their prior numeric cost values.
- Every resolved model exposes a non-blocking compile-time cost-range
  assessment, and applications may opt into strict proof enforcement.
- Actual compact-cost overflow is reported distinctly from `NoPath` and never
  produces a successful incomplete field or cached product.
- Axial hex paths, topology, fields, validation, caching, and sparse behavior
  work over bounded top-down worlds.
- Unit, weighted, cached, and batch paths agree on transition legality and
  optimal costs for every built-in model.
- Path planning and movement commit agree for unchanged world state.
- Explicit provider overloads make the same special edges visible to topology,
  exact paths, reverse fields, and movement commits.
- Topology never returns definitive `Unreachable` for a route exact search can
  take under the same fresh model.
- Reverse fields expose exactly the reverse of legal forward transitions.
- Incremental and full topology builds are equivalent.
- Model-specific invalidation covers every affected chunk without assuming
  face-only adjacency.
- Built-in transition enumeration allocates nothing and uses no runtime
  polymorphism.
- Unsupported lattice, step, and provider combinations fail at compile time
  with focused diagnostics.

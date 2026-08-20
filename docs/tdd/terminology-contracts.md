# Canonical terminology and pre-1.0 contract cleanup

Status: approved for implementation after independent review.

## Problem

Tess has a strong underlying domain model, but its public names and maintained
documentation do not always expose that model consistently. The terminology
audit found three contract-level contradictions:

- `PathStatus::NoPath` is described as proof that no route exists, although
  `MissingChunkPolicy::TreatAsBlocked` can return it after treating unknown
  sparse chunks as impassable;
- `ChunkState` is described as derived from active flags, while public setters
  can make the stored state disagree with those flags; and
- `ChunkPage` promises allocation-free, non-throwing construction and reset
  for an unconstrained field value whose construction or assignment may
  allocate or throw.

The same audit found overloaded or stale terms across public APIs, diagnostics,
comments, maintained docs, examples, and demos. `FrameOps` is an operation
batch rather than a rendered frame; `RouteCacheScratch` owns retained cache
entries rather than only temporary workspace; `version` is often specifically
a content version; `WalkableField` is the lone legacy movement name in an API
that otherwise uses passable/impassable; and several diagnostics still refer to
an MVP, a later slice, or work that will land later.

These are pre-1.0 APIs. The change should establish the clearest durable model
now rather than preserve confusing names through compatibility aliases that
would become part of the 1.x surface.

## Authority and scope

The implementation and public-header contracts are authoritative for
behaviour. Maintained architecture and guide pages describe current truth. A
new terminology page explains the shared language but does not replace the
specialized contracts. This TDD becomes historical design intent after the
change lands; a design-decision fragment records the lasting rationale.

The change covers:

- the three contract contradictions above;
- high-confidence public names whose current noun misstates ownership, scope,
  or meaning;
- strong types where two raw scalar domains are plausibly interchangeable;
- canonical lifecycle, search, ownership, time, and status language;
- site-wide short tooltips backed by a central definition file;
- stale implementation-state language in current comments and diagnostics;
- examples, demos, tests, upgrade guidance, and maintained docs affected by
  those decisions; and
- a project-agent instruction requiring terminology review when a PR changes a
  domain concept.

It does not normalize historical TDDs, decisions, experiment records, or
planning records. It does not rename harmless local variables whose name is
clear in context, introduce user-defined literals, or attempt to make a prose
checker infer semantic consistency.

## Canonical model

### Spatial and storage nouns

- A **tile** is one discrete world location. `cell` remains appropriate for an
  HTML table cell, matrix cell, or other explicitly non-world structure.
- A **chunk** is a logical fixed region of a world.
- A **page** is the storage object currently backing one chunk. Dense worlds
  have a fixed one-to-one relationship; sparse worlds reuse page slots across
  chunk identities.
- A **field** is one typed per-tile column. A **schema** is the compile-time set
  of fields. `component` remains ECS-specific.
- A **coordinate** is a spatial position. A **key** is a lookup identity within
  a stated owner. An **ID** is a semantic or order identity with a stated
  scope. A **handle** or **ticket** is a non-owning reference whose validity
  interval must be documented.

### Search and ownership nouns

- A **search** performs graph work. A **path** is the concrete ordered
  coordinate sequence it returns. A **route** is a retained, reusable, or
  higher-level plan.
- **Scratch** is caller-owned reusable workspace and may own a borrowed result
  until its next mutation. A **product** is retained derived data with explicit
  freshness dependencies. A **cache** is bounded retained storage. A **view**
  or **span** is borrowed access.

### Lifecycle and time

- Sparse storage is **materialized** or made **resident**, then **evicted**.
  `load` is reserved for archive/persistence loading or an application-owned
  backing store.
- A **simulation tick** is one fixed schedule traversal. A **render frame** is
  one real-time caller update that may grant zero or more ticks. An
  **operation batch** is one collection submitted for planning. A **delta
  frame** is one presentation publication and may coalesce multiple ticks.
- **Build** means initial or whole-product construction. **Update** requests
  incremental maintenance. **Rebuild** means an actual full reconstruction.
  **Refresh** validates or updates retained state. Prose uses **topology
  maintenance** when either update or rebuild may occur.

### States and qualifiers

- Terrain is **passable** or **impassable**. `blocked` describes movement
  contention or an unavailable transition; `open` and `closed` are reserved
  for a search frontier unless plain-language tutorial text defines them.
- **Dirty** means work has been declared. **Stale** means retained derived data
  no longer matches its dependencies. **Active** and **sleeping** describe the
  chunk's derived work-participation state.
- Normative prose qualifies overloaded modifiers: source-compatible,
  deterministic order, unchanged world, address-stable, optimal path,
  exact-key lookup, capacity-bounded, budget-bounded, conservative over-report,
  and similar phrases replace bare `stable`, `exact`, `bounded`, or
  `conservative` where the property is otherwise ambiguous.
- **Cooperative asynchronous work** means resumable caller-visible work with no
  hidden thread. Allocation claims name the exact warm/reserved operation and
  include any field-value or callback requirements.

Availability (`released`, `landed on main`, `release-gated`, `deferred`, `out
of scope`), compatibility (`stable`, `optional-stable`, `experimental`,
`implementation-only`), and decision outcome (`accepted`, `rejected
experiment`, `not promoted`) remain separate axes. The roadmap and support
policy own their changing inventories.

## Public API decisions

### Field values and page guarantees

Introduce a named `TileFieldValue` concept for the value type accepted by
`Field<Tag, Value>`. A field value must be an unqualified object type that is
nothrow default constructible, trivially copyable, trivially copy assignable,
and trivially destructible. Requiring a *trivial* default constructor would
incorrectly reject Tess's own `Coord2`, `Coord3`, and `Extent3`, whose default
member initializers are meaningful but non-throwing.

The constraint matches Tess's inline structure-of-arrays storage,
deterministic value initialization, and fixed-capacity sparse page reuse.
Archive persistence keeps its narrower, independently checked scalar-field
subset; `TileFieldValue` does not imply persistability. Pages are
value-initialized, not universally zero-initialized (`Extent3{}` deliberately
has `z == 1`). Page traversal and storage perform no allocation during reset;
the contract does not infer that arbitrary user-written default initialization
is allocation-free from a type trait alone. Construction, reset, and fill
remain `noexcept` for accepted values.

Invalid field values fail at the `Field` public boundary with a library-authored
diagnostic. The synthetic throwing assignment type used only to exercise the
formerly unconstrained `fill_field` contract is removed. `fill_field` becomes
unconditionally non-throwing and performs no allocation for valid field values.

Alternative rejected: allow arbitrary owning values and make page reset
throwing. `SparseResidentWorld::ensure_resident` currently evicts a victim
before reusing its slot; a partial or throwing reset can lose capacity and
leave a page only partly reset. Providing a transactional generic-value reset
would require page-sized duplicate storage or a different sparse ownership
model and would undermine the storage contract for no demonstrated use case.

### Derived chunk lifecycle

Replace the overly broad `ChunkState` with `ChunkActivity::{Sleeping, Active}`.
Activity is no longer stored in `ChunkMeta`; `chunk_activity(key)` derives it
from the world's active mask. Public `set_chunk_state` is removed. Marking and
clearing active flags is the only authority.

Remove stored `ChunkMeta::active_count`, which is another redundant value that
can disagree with the active mask. Diagnostics that need it use a derived
`active_category_count(key)` accessor.

Archive metadata no longer serializes an independently settable chunk state;
the loader derives activity and category count from restored active flags.
This changes the fixed chunk prefix, so it explicitly introduces world archive
format v2. `world_archive_format_version` becomes 2, v2 receives immutable
golden bytes and prefix-size assertions, and v1 input returns
`UnsupportedFormat` from both inspection and load unless an actual migration
reader is deliberately added. The support policy, persistence architecture,
fuzzer, corruption cases, fixtures, upgrade guide, and release notes all move
to the v2 contract. It removes an invalid state rather than adding
reconciliation precedence.

### Strong scalar domains

Introduce lightweight explicit value types for `DirtyMask`, `ActiveMask`,
`ContentVersion`, `TopologyVersion`, and `ResidencyGeneration`. Dirty and
active masks are 32-bit bit sets with named empty and bitwise operations and no
implicit cross-conversion. Content and topology versions are 64-bit,
practically monotonic values: preserving 32-bit wrapping would leave a
plausible ABA hole in long-running dirty observations and cache freshness.
Advancing any version at `uint64_t` maximum fails fast rather than wrapping.
Residency generations use the same overflow rule and reserve zero for
absent/invalid, so an advance never emits the zero sentinel.

Every wrapper is standard-layout and trivially copyable with pinned size and
alignment. None converts implicitly to another wrapper or integer; explicit
raw extraction occurs only at serialization, hashing, and external-adapter
boundaries. `ChunkMeta::version` becomes `content_version`; public dependent
spellings follow it rather than retaining generic or misleading names:
`TileChunkDelta::content_version`,
`MovementVersionCheck::{from,to}_content_version`, content-version dependency
types, and stale-content statuses/counters all name the domain explicitly.
Dirty observations, route dependencies, topology products, and movement checks
use the corresponding strong types. Residency generation applies only to
sparse residency intervals, not path tickets, async slots, or delta-frame view
generations.

World archives do not serialize dirty history, content versions, topology
versions, or residency generations; loading advances fresh invalidation state,
and its dirty mask is a caller-supplied invalidation input rather than archived
data. Format v2 continues to encode active mask bits as fixed-width
little-endian integers. Delta frames are a separate public data boundary and
participate in the strong-type migration.

Alternative rejected: user-defined literals. Masks and versions are usually
computed or application-defined rather than human-authored physical units;
explicit named values communicate the domain without adding global literal
suffixes.

### Ownership- and scope-accurate names

- `RouteCacheScratch`, `RouteCacheLimits`, and `RouteCacheStats` become
  `UnitRouteCache`, `UnitRouteCacheLimits`, and `UnitRouteCacheStats`. The
  public identity reflects both retained ownership and the cache's unit-cost
  scope.
- `FrameOps` becomes `OperationBatch`. Handles and IDs are documented as
  batch-local and valid only until `clear()`; `stable handle` is removed.
- `movement::WalkableField` becomes
  `movement::UnitCostFieldMovement`; `WalkableCostField` becomes
  `PositiveCostFieldMovement`. Both names identify movement-class adapters
  rather than storage fields.
- `MissingChunkPolicy` becomes the parallel pair `AssumeImpassable` and
  `ReportIndeterminate`; `ReportIndeterminate` is the default for public path
  APIs. Dense worlds are unaffected. Sparse callers must explicitly opt into
  treating unknown space as impassable before receiving a policy-relative
  `NoPath` across a non-resident boundary.
- `MovementStatus::BlockedFrom` and `BlockedTo` become `ImpassableFrom` and
  `ImpassableTo`, with matching public counters and terrain diagnostics.
  `PathAgentPhase::Blocked` remains correct for an agent temporarily unable to
  progress, and `TransitionAvailability::Blocked` remains correct for an
  unavailable transition.

No compatibility aliases retain the superseded names. The upgrade guide maps
each old spelling to the new one.

Remove `movement::LegacyWeighted` and the weighted
`<World, PassableTag, CostTag>` forwarding overloads. Weighted callers name a
movement class explicitly, normally `PositiveCostFieldMovement`. A caller that
truly needs passability independent of a zero cost can compose
`MovementClass<Field<PassableTag>, FieldCost<CostTag>>` explicitly. This
removes the asymmetric compatibility path rather than carrying a type named
`LegacyWeighted` into 1.0. Rename the current `mvp_path` example and target as
part of the same current-language cleanup.

### Reachability result semantics

`PathStatus::NoPath` remains the conventional name, but its contract becomes
policy-relative: no route exists in the graph considered under the selected
missing-chunk policy. `Indeterminate` means a non-resident boundary prevented a
definitive whole-world answer. Dense searches and completed sparse searches
that did not ignore an unknown boundary can treat `NoPath` as global.

This changes no search algorithm. The selected policy must reach every
sparse-capable unit and weighted A*, cached A* (including negative entries),
boxed and product distance-field, weighted-batch, `PathRequestRuntime`,
topology-precheck, and path-agent entry point. No runtime or batch layer may
silently substitute `AssumeImpassable`. Tests pin the matrix of distinct
policy outcomes and the maintained docs no longer make an unconditional
global-proof claim.

## Documentation and tooltip design

Add `docs/terminology.md` under Concepts. It is a concise human-readable
reference organized by nouns, actions, states, qualifiers, time, and project
status. Entries state preferred wording, discouraged ambiguous wording, the
meaning, and links to the authoritative architecture/API page.

Enable Material's `abbr`, `pymdownx.snippets`, and `content.tooltips` features.
`includes/abbreviations.md`, outside `docs/`, supplies short plain-text
definitions auto-appended to every page. Only exact, unambiguous phrases such
as `simulation tick`, `operation batch`, `content version`, and `residency
generation` receive global definitions. Bare overloaded words such as
`stable`, `exact`, `bounded`, `active`, and `frame` never receive a universal
tooltip. Tooltips supplement visible definitions and first-use explanations;
they are not the sole source of a contract.

Configure `watch: [includes]` so local serving reloads definition changes and
set `pymdownx.snippets.check_paths: true` so a missing shared definition file
fails instead of silently removing all global definitions.

Update public comments, diagnostics, maintained documentation, examples, and
demos to use the canonical model. Remove current-state references to `MVP`,
`later slice`, `lands later`, and similar chronology. A diagnostic names the
unsupported combination, current boundary, and available alternative where
one exists.

The project `AGENTS.md` gains a conditional terminology-consistency
requirement for PRs that introduce, rename, or redefine a domain concept. It
requires review of public APIs, comments, diagnostics, maintained docs,
examples, demos, and relevant tests; updates the terminology and tooltip
sources; and explicitly exempts harmless locals and historical records.

No semantic prose checker is introduced. Exact retired public spellings can be
searched during implementation and review, but a green substring checker
cannot establish that context-sensitive language such as `exact` is truthful.

## Migration and compatibility

This is a deliberate pre-1.0 source break and archive-format-v2 transition.
The 1.0 upgrade guide lists every public rename, strong-type construction
change, removed state/count field and setter, policy-default change, removed
weighted forwarding overload, and archive consequence. Release notes describe
the adopter-visible outcome.
No deprecated aliases extend the ambiguous vocabulary into the 1.x contract.

The change does not alter path optimality, ordering, movement rules, world
geometry, cache policy, or scheduling cadence. The safer sparse default may
return `Indeterminate` where an omitted policy previously returned `NoPath`.
Valid field schemas retain their inline layout and performance
characteristics; content and topology versions deliberately widen.

## Verification

- Add compile-time positive checks for scalar and enum values, `Coord2`,
  `Coord3`, `Extent3`, and a fixed-size aggregate. Reject strings, vectors,
  references, cv-qualified values, and throwing assignment at the `Field`
  boundary with a library-authored compile-fail diagnostic.
- Pin size, alignment, standard-layout/trivial-copy properties, explicit raw
  extraction, zero-sentinel behavior, and rejection of every cross-domain
  strong-type conversion.
- Keep page construction/reset and dense fill `noexcept` for all accepted
  values. Allocation tests prove Tess-owned page storage and traversal do not
  allocate with representative non-allocating values; they do not claim an
  arbitrary accepted user-written nothrow initializer cannot allocate.
- Test that chunk activity and active-category count derive from active-mask
  transitions and cannot be restored independently from an archive.
- Add the archive-v2 golden and prefix contract, v1 unsupported-format cases,
  corruption coverage, fuzz coverage, and immutable-fixture updates.
- Test sparse page reuse after eviction: default field values, capacity,
  directory and LRU state, handle invalidation, and nonzero generation.
- Test both sparse missing-chunk policies and their documented `NoPath` versus
  `Indeterminate` interpretation across the complete runtime/cache/batch/field/
  precheck/agent matrix, including negative route-cache entries.
- Compile and run every affected example and demo model after applying public
  renames.
- Build MkDocs strictly, inspect the generated abbreviation markup and
  terminology page, run link, snippet, output, public-surface, and Mermaid
  checks, and exercise the site at desktop and narrow widths with keyboard and
  touch-equivalent interaction where relevant.
- Search current authoritative surfaces for the retired public spellings and
  stale project-chronology phrases using an intentional historical allowlist.
  Historical changelogs, TDDs, decisions, planning records, compatibility
  commentary, and genuine old-format descriptions retain their original
  language.
- Update public-surface manifests, installed consumers, compatibility fixtures,
  upgrade examples, Doxygen, leaf/umbrella header cells, renamed example
  targets, and synchronized documentation snippets.
- Run focused storage, persistence, path, cache, queued-operation, scheduling,
  diagnostics, and documentation tests locally, then the repository's full
  pre-commit validation. CI remains authoritative for GCC, MSVC, sanitizer,
  exception-free, WebAssembly, documentation, and compatibility cells.
- Add release-facing and design-decision fragments. Mark this TDD implemented
  and historical only after the maintained code and documentation land.

## Review questions

1. Does the reset-oriented `TileFieldValue` contract preserve every intended
   field use while protecting sparse slot reuse?
2. Are the five strong scalar domains and their exact widths/overflow rules
   proportionate to the mix-ups and ABA failures they prevent?
3. Is archive format v2 with active flags as the sole activity authority the
   cleanest pre-1.0 transition, or is a v1 migration reader worth retaining?
4. Do `UnitRouteCache`, `OperationBatch`, `UnitCostFieldMovement`,
   `PositiveCostFieldMovement`, and the parallel missing-chunk policies create
   the clearest 1.0 vocabulary?
5. Is removing the asymmetric weighted tag-pair convenience preferable to
   preserving it under a new non-legacy domain name?
6. Are the tooltip terms narrow enough to remain correct wherever auto-appended
   definitions match?

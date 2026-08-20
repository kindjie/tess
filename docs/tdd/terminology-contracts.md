# Canonical terminology and pre-1.0 contract cleanup

Status: proposed for independent review.

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
`Field<Tag, Value>`. A field value must be trivially default constructible,
trivially copyable, trivially copy assignable, and trivially destructible.
The constraint matches Tess's inline structure-of-arrays storage, deterministic
zero/default initialization, fixed-capacity sparse page reuse, and scalar
persistence model. It makes the existing non-throwing, allocation-free page
construction/reset promise true rather than weakening sparse-world exception
safety.

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

`ChunkState` remains the readable result type, but it is no longer stored in
`ChunkMeta`. `chunk_state(key)` derives `ResidentSleeping` or `ResidentActive`
from the world's active mask. Public `set_chunk_state` is removed. Marking and
clearing active flags is the only authority for the state.

Archive metadata no longer serializes an independently settable chunk state;
the loader derives it from restored active flags. This intentionally breaks
pre-1.0 archives and is recorded in the upgrade guide and release notes. It
removes an invalid state rather than adding reconciliation precedence.

### Strong scalar domains

Introduce lightweight explicit value types for `DirtyMask`, `ActiveMask`,
`ContentVersion`, `TopologyVersion`, and `ResidencyGeneration`. The wrappers
remain aggregate-like value types, expose their underlying unsigned value, and
provide only the bitwise or increment/equality operations meaningful to that
domain. Public APIs no longer accept interchangeable raw dirty and active
masks. `ChunkMeta::version` becomes `content_version`; observations,
dependencies, movement checks, diagnostics, and archive code use the same
name and type.

The archive continues to encode the underlying fixed-width integers. This is
a source-level type/name cleanup, not a width or byte-order change except for
the independent chunk-state field removed above.

Alternative rejected: user-defined literals. Masks and versions are usually
computed or application-defined rather than human-authored physical units;
explicit named values communicate the domain without adding global literal
suffixes.

### Ownership- and scope-accurate names

- `RouteCacheScratch` becomes `RouteCache`. It may continue to own internal
  query scratch, but its public identity reflects its retained entries.
- `FrameOps` becomes `OperationBatch`. Handles and IDs are documented as
  batch-local and valid only until `clear()`; `stable handle` is removed.
- `movement::WalkableField` becomes `movement::PassableField`, and
  `WalkableCostField` becomes `PassableCostField` if the latter remains part of
  the public compatibility identity surface.
- `MissingChunkPolicy::TreatAsBlocked` becomes `TreatAsImpassable`.

No compatibility aliases retain the superseded names. The upgrade guide maps
each old spelling to the new one.

### Reachability result semantics

`PathStatus::NoPath` remains the conventional name, but its contract becomes
policy-relative: no route exists in the graph considered under the selected
missing-chunk policy. `Indeterminate` means a non-resident boundary prevented a
definitive whole-world answer. Dense searches and completed sparse searches
that did not ignore an unknown boundary can treat `NoPath` as global.

This changes no search algorithm. Tests pin the distinct sparse-policy
outcomes and the maintained docs no longer make an unconditional global-proof
claim.

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

This is a deliberate pre-1.0 source and archive break. The 1.0 upgrade guide
lists every public rename, strong-type construction change, removed setter,
and archive consequence. Release notes describe the adopter-visible outcome.
No deprecated aliases extend the ambiguous vocabulary into the 1.x contract.

The change does not alter path optimality, ordering, movement rules, world
geometry, cache policy, or scheduling cadence. Strong wrappers retain existing
integer widths and wrap behaviour. Valid scalar field schemas retain their
layout and performance characteristics.

## Verification

- Add compile-time positive and negative checks for `TileFieldValue`, the new
  strong scalar domains, and rejection of accidental dirty/active-mask mixing.
- Keep page construction/reset and dense fill allocation-free and `noexcept`
  for all accepted values; run the existing allocation-sensitive tests.
- Test that chunk state follows active-mask transitions and cannot be restored
  independently from an archive.
- Test both sparse missing-chunk policies and their documented `NoPath` versus
  `Indeterminate` interpretation.
- Compile and run every affected example and demo model after applying public
  renames.
- Build MkDocs strictly, inspect the generated abbreviation markup and
  terminology page, run link, snippet, output, public-surface, and Mermaid
  checks, and exercise the site at desktop and narrow widths with keyboard and
  touch-equivalent interaction where relevant.
- Search current authoritative surfaces for the retired public spellings and
  stale project-chronology phrases. Historical records are excluded and retain
  their original language.
- Run focused storage, persistence, path, cache, queued-operation, scheduling,
  diagnostics, and documentation tests locally, then the repository's full
  pre-commit validation. CI remains authoritative for GCC, MSVC, sanitizer,
  exception-free, WebAssembly, documentation, and compatibility cells.
- Add release-facing and design-decision fragments. Mark this TDD implemented
  and historical only after the maintained code and documentation land.

## Review questions

1. Is constraining field values the correct storage contract, or is arbitrary
   owning per-tile state an intended use case worth redesigning sparse reset
   around?
2. Should all five strong scalar domains land together, or does any wrapper
   add ceremony without preventing a credible domain mix-up?
3. Is removing serialized chunk state preferable to retaining and validating a
   redundant field for pre-1.0 archive migration?
4. Do `RouteCache`, `OperationBatch`, `PassableField`, and
   `TreatAsImpassable` accurately name the public concepts without creating a
   worse collision or ambiguity elsewhere?
5. Are the tooltip terms narrow enough to remain correct wherever auto-appended
   definitions match?

# Pathfinding strategy comparison demo

Status: Implemented after independent design and implementation review;
historical design intent.

## Problem and contract

The pathfinding strategy comparison explains four reuse shapes with source
snippets and benchmark tables, but readers cannot see the work-sharing model.
The embedded demo must run the same C++ scenario implementation as the native
self-checking example and make these distinctions visible:

- independent A* performs one search per request;
- an exact route-cache repeat turns the second lookup into a hit with no
  search expansion;
- a weighted batch groups requests and builds one field for their shared goal;
- a unit-cost distance field builds once and reconstructs all matching paths.

The C++ result paths, statuses, costs, expansion counts, and reuse counters are
authoritative. JavaScript may animate and label those snapshots but must not
reimplement pathfinding or invent measurements. The demo is teaching evidence,
not a browser benchmark; the article's native benchmark table remains the
performance evidence.

## Scope and non-goals

The change adds a fixed 16x16 obstacle course, an embedded visualization,
native and browser verification, and maintained hosting/example documentation.
It does not add editable maps, timing in WebAssembly, new public library APIs,
or a generic visualization framework. The existing editable A* demo at
`/demo/` remains unchanged.

## Responsibilities and data flow

The existing comparison example is split along the repository's established
native/model/WebAssembly seam:

1. A platform-neutral C++ model owns the world, invokes the four tess APIs,
   and immediately copies every borrowed result path into fixed-capacity value
   snapshots before the owning scratch is mutated again.
2. The native executable calls the model and checks every documented invariant.
3. A thin C ABI exposes read-only snapshot fields and the fixed passability
   map to Emscripten. It contains no pathfinding calls.
4. JavaScript reads snapshots once, renders four cards, and animates explanatory
   stages such as "three searches" or "one field build". It never feeds state
   back into C++.
5. A dedicated static page is published at `/demo/strategies/` and embedded in
   the comparison article with a titled, same-origin iframe.

The model lives under `examples/` and is compiled into both the native example
and the WebAssembly artifact. Article snippets continue to be synchronized
from that model source, so the code shown, the native checks, and the browser
results share one authority.

## Snapshot contract

Four immutable strategy snapshots expose a bounded interface:

- strategy identifier and request count;
- per-request status, cost, operation-specific expansion count, and copied
  path coordinates;
- route-cache hits and misses where applicable;
- weighted-batch unique goals, field builds, and A* fallbacks where applicable;
- distance-field build expansions and reached-node count where applicable.

The model also exposes checked passability for the scenario's 16x16 cells.
Three vertical walls have alternating single-tile gaps, forcing every strategy
to solve a legible route while keeping the unit-cost workload identical. The
browser renders those C++-reported cells as obstacles; it does not own a
duplicate map.

There is no aggregate expansion total or cross-strategy work ranking. In
particular, weighted batches expose field-build counts but not the shared
field's expansion total; their result `expanded_nodes` describes path
reconstruction rather than the hidden build. The UI labels each counter by its
own API operation and leaves performance comparison to the native benchmarks.

The model uses fixed-capacity arrays bounded by the 16x16 scenario and provides
checked accessors for every strategy, request, and path-node dimension. Each
accessor returns an optional value. The native self-check validates every
copied coordinate only after all strategies have run and their scratch objects
have been reused or destroyed, then checks the invalid-index paths.

The C ABI accepts and returns only `int32_t`. It exports no pointers, spans,
`size_t`, 64-bit coordinates, or C++ enum representations. Readiness is `1`
only after all four model snapshots pass their invariants, `0` before model
initialization, and `-1` after initialization failure. Every other accessor
returns exactly `-1` for an uninitialized model or an invalid strategy,
request, or path-node index. Path statuses are translated to an explicitly
numbered browser result code. JavaScript validates the expected counts,
status codes, coordinate bounds, and absence of sentinels before it sets
`data-tess-strategies="ready"`; malformed data sets the state to `failed`.

The existing `tess_pathfinding_strategies` executable becomes shared model plus
native main; no new native executable is added, so the exact 16-example CI
contract remains unchanged. Independent A* is migrated from one request to
three calls for the three shared-goal starts. The route-cache workload remains
one exact request called twice. Batch remains one `weighted_path_batch` call
that groups three requests internally. Distance field remains one explicit
`build_distance_field` call followed by three caller-controlled
`distance_field_path` reads. Snippet markers move with those authoritative
calls into the shared model source, and the article/source links follow them.

These fixed workloads are deliberately labelled because they are not
identical. The animation and persistent text both expose their call topology:
three independent calls; a miss followed by a hit; one batch call that
internally groups three requests and returns scratch-backed paths; and one
caller-owned build followed by three reads. This prevents batch and distance
field from appearing equivalent merely because both produce three routes from
one shared-goal world.

## Presentation and accessibility

The iframe contains replay and pause controls, reduced-motion support, four
responsive cards, DOM/CSS tile grids, call-topology strips, data-product
labels, and a persistent text comparison. The strips distinguish three
independent searches, a miss-then-hit cache sequence, one grouped batch, and
one explicit field build followed by three reads. The distance-field card
marks every reachable cell only after the C++ reached-node count equals the
passable-cell count; it does not invent per-cell scalar values. Tile grids are
`aria-hidden`; adjacent text carries the request shape, phase
sequence, endpoints, path lengths, statuses, operation-specific counters, and
reuse facts. Pause exposes its state with `aria-pressed`. The polite live
region announces phase changes only, not animation frames. Keyboard users can
operate every control, and `prefers-reduced-motion` renders the complete final
state immediately.

The animation visualizes call stages and returned routes. Search-frontier
geometry is out of scope because the public results expose counts, not the
order in which nodes were visited; drawing a fabricated frontier would violate
the authority contract.

The article uses a titled, lazy-loaded, same-origin iframe followed by a visible
standalone-demo link. The embedded page has no horizontal overflow at 320 px,
400 px, or desktop widths. Four compact grids use two columns on wider screens
and one card per row below 520 px so topology and data-product labels remain
legible. Article CSS assigns documented desktop and narrow iframe heights so
content is neither clipped nor hidden behind a nested horizontal scroller. No
`postMessage` auto-height protocol is added unless fixed responsive sizing
proves inadequate during validation.

## Build, deployment, and failure behavior

`tools/build_web_demo.sh` compiles the model and adapter with the pinned
Emscripten image already used by the documentation workflow, then places the
page under the generated Pages artifact. The article references that generated
path. If WebAssembly fails to load or the model self-check fails, the page sets
an explicit failed readiness state and leaves a readable error instead of
showing placeholder results.

The iframe is not a dependency of MkDocs authoring: a plain local MkDocs server
still renders the article and fallback link, while a full demo preview runs the
existing WebAssembly build followed by a static server over `build/site`.

## Verification and acceptance evidence

- Native model tests verify all paths and operation-specific counters, copied
  coordinates after scratch reuse/destruction, invalid checked reads, and the
  expected workload labels.
- Narrow source-contract tests guard that the browser adapter links the shared
  model, contains no direct tess pathfinding calls, and retains the iframe and
  accessible text representation; these checks are structural evidence, not a
  claim that the browser behavior ran.
- Strict MkDocs, snippet, and generated-link checks pass.
- A real Emscripten build is polled in headless Chrome by wall time and reaches
  a ready state only after all four snapshots have been read and validated.
- Captured DOM evidence starts with empty generated result cells and, after
  readiness, includes four successful strategies plus C++-derived values: one
  cache hit, one cache miss, one batch field build, and zero batch fallbacks.
- Manual local validation covers desktop, 400 px and 320 px viewports, keyboard
  controls, reduced motion, no horizontal clipping, and the article embed.
- A release-facing changelog fragment records the demo. After acceptance this
  TDD is marked implemented and historical; maintained example and hosting
  documentation describe the shipped behavior.

## Alternatives considered

Extending the editable `/demo/` page would mix a single-query sandbox with a
fixed multi-strategy lesson and make both interfaces less coherent. A
JavaScript-only animation would be smaller, but it could drift from tess and
would not satisfy the source-authority requirement. Instrumenting internal
search-frontier callbacks would add a new library surface solely for a teaching
visual; the stable result-and-counter snapshots provide the needed lesson with
less authority and maintenance cost.

## Rollback and maintained truth

The demo is additive. Rollback removes the iframe and generated demo target
without changing library behavior. After implementation, this TDD becomes
historical intent; `docs/examples.md`, `docs/hosting.md`, the C++ model, and the
browser smoke test describe current behavior.

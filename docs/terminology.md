# Terminology

Tess uses a small domain language so that API names, diagnostics, and
documentation describe the same concepts. Prefer the terms below when naming
application adapters or discussing Tess behaviour. The linked concept pages
remain authoritative for the full contracts.

## Spatial and storage nouns

| Prefer | Avoid when referring to Tess | Meaning |
| --- | --- | --- |
| **tile** | cell | One discrete world location. “Cell” remains suitable for a table, matrix, or other non-world structure. See [storage](architecture/storage.md). |
| **chunk** | page | A logical fixed region of a world. See [shapes](architecture/shape.md). |
| **page** | chunk, when storage ownership matters | The storage object backing a chunk. Sparse worlds reuse page slots across chunk identities. See [residency](guide/residency.md). |
| **field** | component | One typed per-tile column. “Component” is reserved for an ECS. See [storage](architecture/storage.md). |
| **schema** | layout | The compile-time set of fields in each page. See [storage](architecture/storage.md). |
| **coordinate** | key, ID | A spatial position. A key is a lookup identity; an ID is a semantic or ordering identity. See [shapes](architecture/shape.md). |
| **handle** or **ticket** | pointer | A non-owning reference with a documented owner and validity interval. |

## Search and ownership nouns

| Prefer | Avoid | Meaning |
| --- | --- | --- |
| **search** | path | Graph work performed to answer a request. See [pathfinding](architecture/path.md). |
| **path** | route, when describing the returned coordinates | A concrete ordered coordinate sequence. |
| **route** | path, when describing retained planning state | A retained, reusable, or higher-level plan. |
| **scratch** | cache | Caller-owned reusable workspace. A borrowed result may remain valid only until the scratch is mutated again. |
| **product** | cache | Retained derived data with explicit freshness dependencies. |
| **cache** | scratch | Bounded retained storage that may reuse a previous result. |
| **view** or **span** | result storage | Borrowed access whose owner and invalidation point must be stated. |

## Metadata and path-model nouns

| Term | Meaning |
| --- | --- |
| **dirty mask** | Application-defined work categories declared for one chunk. See [storage](architecture/storage.md). |
| **active mask** | Application-defined work-participation categories for one chunk. |
| **content version** | A practically monotonic value advanced when authoritative chunk content changes. |
| **topology version** | A practically monotonic value advanced when topology-relevant state changes. |
| **residency generation** | The identity of one interval during which a sparse chunk remains resident. |
| **movement class** | A compile-time definition of passability, entry cost, and allowed steps. See [topology](architecture/topology.md). |
| **distance field** | Retained shortest-distance data built from one or more goals for repeated path reads. See [pathfinding](architecture/path.md). |
| **route cache** | Capacity-bounded retained storage for reusable route results. |

## Actions

| Prefer | Avoid | Meaning |
| --- | --- | --- |
| **materialize** or **make resident** | load | Give a sparse chunk a page. Reserve “load” for persistence or an application backing store. See [residency](guide/residency.md). |
| **evict** | unload | End a sparse chunk’s residency interval and make its page reusable. |
| **build** | update | Construct a complete derived product. |
| **update** | rebuild | Request incremental maintenance. |
| **rebuild** | refresh | Perform a full reconstruction. |
| **refresh** | rebuild | Validate or update retained state against its dependencies. |
| **topology maintenance** | rebuild, when either path is possible | An update or full rebuild selected by the topology implementation. See [topology](architecture/topology.md). |

## States

| Prefer | Avoid | Meaning |
| --- | --- | --- |
| **passable / impassable** | open / blocked | Whether terrain permits entry. “Open” and “closed” describe a search frontier; “blocked” describes contention or an unavailable transition. |
| **dirty** | stale | Work has been declared for a chunk and category. See [queued operations](architecture/queued-operations.md). |
| **stale** | dirty | Retained derived data no longer matches its dependencies. |
| **active / sleeping** | awake / inactive | Chunk work-participation state derived from its active mask. See [simulation](architecture/simulation.md). |
| **indeterminate** | no path | Non-resident space prevented a definitive reachability answer. `NoPath` means no route in the graph considered under the selected missing-chunk policy. See [pathfinding](architecture/path.md). |
| **not computed** | no path | No current result exists because an operation has not run or a retained product is cleared, stale, or mismatched. |
| **no candidate** | no path | A bounded or heuristic strategy found no candidate; exact search is still needed for a reachability conclusion. |
| **blocked transition** | impassable destination | A transition is unavailable even though its endpoint tiles remain passable. |

## Time and grouping

| Term | Meaning |
| --- | --- |
| **simulation tick** | One fixed traversal of the sealed schedule. |
| **render frame** | One real-time caller update, which may grant zero or more simulation ticks. |
| **operation batch** | One collection submitted together for planning. Its handles and IDs are batch-local. |
| **delta frame** | One presentation publication, which may combine multiple simulation ticks. |
| **cooperative asynchronous work** | Resumable caller-visible work with no hidden thread. |

See [simulation and scheduling](architecture/simulation.md) and
[presentation](guide/presentation.md) for the relationships among these
units.

## Qualifiers

Words such as “stable,” “exact,” “bounded,” and “conservative” do not identify
a useful contract on their own. State the property instead: for example,
**source-compatible**, **deterministic order**, **unchanged world**,
**address-stable**, **optimal path**, **exact-key lookup**,
**capacity-bounded**, **budget-bounded**, or **conservative over-report**.

Allocation claims similarly name the operation and its conditions, such as
“no Tess-owned allocation during a reserved warm search.”

## Project status

Keep these independent:

- **availability:** released, landed on `main`, release-gated, deferred, or out
  of scope;
- **compatibility:** stable, optional-stable, experimental, or
  implementation-only; and
- **decision outcome:** accepted, rejected experiment, or not promoted.
  The v1.0 prototype-queue synthesis refines accepted work by tier --
  private optimization, supported behavior, public API, deferred
  research, or rejected mechanism -- which maps onto these outcomes
  (the first three refine "accepted"; deferred research refines "not
  promoted"; rejected mechanism is "rejected experiment").

The [roadmap](roadmap.md) owns availability. The [support policy](support.md)
owns compatibility. Historical decisions and experiments explain outcomes but
do not describe current implementation state.

## Movement outcomes

The movement measurement vocabulary, coined by the v1.0 experiment
stream and now used by maintained architecture pages and permanent test
suites:

- A **settle** runs the movement tier until a **no-progress fixpoint**:
  no agent position has changed for a configured window of consecutive
  ticks (`wedge_ticks`, 16 by default in the pinned harness), or a
  tick cap ends the run first (**censored**). Classification happens
  after termination, not during it. **Settle ticks** count that run's
  simulation ticks.
- **Terminal classification** assigns every agent one of five
  categories at the fixpoint: **arrived** (on its goal),
  **goal-occupied** (its goal is permanently held by another settled
  agent), **sealed** (its goal is unreachable under the terrain and
  the terminal-agent set together -- the harness separately counts
  **structural seals**, goals bare terrain already disconnects, so
  experiments subtract those rather than credit a movement arm for
  them; distinct from the *sealed schedule* of the simulation-tick
  vocabulary above), **wedged** (live but permanently unable to make
  progress), or **censored** (the tick cap ended the settle first).
- The web-colony demo's own recovery classifier uses a distinct
  three-way vocabulary -- **arrived**, **crowd-blocked**, **durably
  unreachable** -- which stays distinct; do not translate between the
  two informally.
- **Arming** a mechanism or recipe means enabling it for a run (an
  armed arm vs the canonical arm); this is unrelated to *goal arming*
  in the ECS adapter vocabulary.
- For terrain, prefer **passable / impassable** here as everywhere;
  "open" remains reserved for search frontiers.

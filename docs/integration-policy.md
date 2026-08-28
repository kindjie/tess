---
description: >-
  How tess takes optional dependencies: two-gate builds, pinned
  versions, and adapters that never enter the dependency-free core.
---

# Integration policy

What tess guarantees to a consumer, and what it deliberately does not.
Every statement here is backed by code or a test; where the honest
answer is "not guaranteed" or "not tested", it says so rather than
leaving you to find out.

## Exceptions

tess supports two build-wide compiler modes. Exception-enabled C++20 remains
the default. Clang-family and GCC consumers may compile every translation unit
with `-fno-exceptions`. Native MSVC consumers use `/EHs-c-` together with
`_HAS_EXCEPTIONS=0`. The latter is an undocumented and unsupported MSVC STL
implementation switch, so this mode is version-sensitive and verified against
the CI toolset rather than guaranteed by Microsoft. Aggregate headers,
representative runtime behavior, installed and FetchContent consumers, and
full Clang/GCC examples are tested in exception-free configurations. The
installed `tess::tess` target never forces either policy.
`TESS_HAS_EXCEPTIONS` and `tess::has_exceptions` report the compiler mode and
cannot be overridden.

Exceptions tess throws itself:

- `std::length_error` from legacy portal-segment cache operations when a
  deterministic capacity limit is exceeded.
- `std::bad_alloc` from block-scratch capacity overflow and from allocation,
  including implicitly from container growth.

Checked capacity entry points report pre-allocation size failures in either
mode. In an exception-free build, legacy wrappers abort for those
Tess-detected failures. General allocation failure is not converted to a
status and has no recovery guarantee.

Exceptions you can also receive through tess:

- **Anything your own callback throws.** Callback exceptions propagate
  verbatim; tess neither swallows nor translates them.
- `std::system_error` if thread construction fails while an executor is
  starting up.

In an exception-free build, application callbacks and all other supplied
operations must not throw. Allocation failure, thread-creation failure, and a
throwing callback are outside the supported recovery and state contract. Tess
does not install a new handler or terminate handler.

What is guaranteed when something throws:

- A phase executor **joins every in-flight callback before
  propagating**, so no callback is still running when the exception
  reaches you.
- Queued-operation planning pre-validates, so a policy mismatch means
  nothing executes rather than a partially applied plan.
- Schedule tasks either side of a throwing task keep their triggers
  coherent.
- A maintenance callback exception consumes the invocation that entered the
  callback. Reentrant follow-ups coalesced into that same synchronous
  call-local invocation are consumed with it; independently retained accepted
  maintenance offers remain reachable. The callback owner must keep its
  authoritative dirty-mask and content-version state suitable for an explicit
  retry decision.

Those propagation and rollback guarantees apply to exception-enabled builds.
The exception-free path performs successful work and status-based rollback
directly, without catch-based recovery.

The explicit `NoThrowWorkerPoolPhaseExecutor` and
`NoThrowScopedThreadPhaseExecutor` aliases require callbacks typed `noexcept`
in exception-enabled builds and reject other callback types at compile time.
Queued and result-channel adapters reserve their internal dispatch storage
before preserving that no-throw property through the worker boundary.

What is **not** guaranteed:

- If several callbacks throw concurrently, **which** exception
  propagates is unspecified.
- There is no library-wide strong exception guarantee. Individual
  operations document their own rollback behaviour; absent that, assume
  the basic guarantee.
- Assertion failures never throw, so `noexcept` functions stay honest.
  But note **when** they fire: assertions default to enabled only while
  `NDEBUG` is absent. Release and benchmark builds define `NDEBUG`, so
  the macros compile to nothing and the preconditions on unchecked hot
  accessors go **unverified in production** — a violation is undefined
  behaviour there, not an abort. Do not design around assertions as a
  fail-fast safety net in a release build; use the checked entry points
  (`try_resolve`, `try_field`, plan validation), which validate at
  runtime in every configuration. Define `TESS_ENABLE_ASSERTS=1`
  explicitly if you want the checks in an optimised build.

Some lifecycle and ownership violations are stronger than unchecked hot-path
preconditions: continuing would publish a plausible but false result, orphan
retained accounting, or mutate an object during its own dispatch. Those APIs
call Tess's unconditional fail-fast path in every build and name the violated
contract on stderr. Examples include detectable invalid
`PathRequestRuntime::result` tickets, invalid schedule registration or
dispatch state, reentrant `ResumableWorkQueue` mutation, and rebinding a
nonempty flow-accounted queue or event stream. Checked alternatives remain the
right choice where uncertainty is expected, such as
`PathRequestRuntime::try_result`.

**Build-wide macros must be build-wide.** `TESS_ENABLE_ASSERTS` changes the
bodies of inline functions — 14 in `storage/world.h` alone — and
`TESS_ENABLE_DIAGNOSTICS` changes public *types*: `PathCounters`,
`TraceBuffer`, `WarningSink` and six more exist only when it is defined.
Defining either for some translation units and not others violates the
one-definition rule. No compiler diagnoses it; the linker keeps one
arbitrary definition, so the checks or the members silently vanish from the
other half of your program. Set them for a whole binary or not at all.

On MSVC each is stamped with `#pragma detect_mismatch`, which turns the
mismatch into a link error. GCC and Clang have no equivalent mechanism, so
consistency there is your build system's job.

Native MSVC's exception-free configuration is supported by construction, not
as an equivalent to the stronger GCC/Clang compiler mode. An exception that
nevertheless escapes application or standard-library code remains outside the
contract. Mixed exception modes within one program are unsupported and are
not reliably diagnosed by MSVC at link time; consumers must enforce one mode
across every translation unit. See
[Exception-free builds](architecture/no-exceptions.md) for the complete
failure contract and standard-library operation inventory.

## RTTI

tess contains no `dynamic_cast` and no `typeid`. Type identity where it
is needed at runtime comes from `tag_identity<T>()`, which returns the
address of a per-type static — that facility exists precisely so the
library does not depend on RTTI.

Virtual declarations appear only in the experimental maintenance layer.
The stable `ImmediateScheduler` spelling shares that measured concrete
implementation, but neither documents nor stabilizes the experimental
`MaintenanceScheduler` base or polymorphic use. Virtuals need vtables, not
RTTI.

Stable surfaces are compiled and exercised without RTTI on every supported
compiler family. `tag_identity` is unique only within one linked image. Its
tokens must not be compared, persisted, or transferred across a dynamic
library boundary. Public caches, graphs, payloads, and products that retain
such tokens repeat this restriction on their type documentation.

## Determinism across thread counts

For **deterministic, operation-local kernels** — callbacks whose
observable behaviour depends only on the chunk and inputs they are
handed — identical inputs produce identical results whether operations
run serially or on a worker pool, and regardless of worker count.

That condition is load-bearing, not boilerplate. The callback object is
shared across workers, so a kernel that is merely thread-*safe* can
still be order-dependent: one that stamps chunks with an atomic
counter, or accumulates into shared state, will observe worker
scheduling. Disjoint writes prevent data races; they do not make an
arbitrary kernel deterministic. Determinism is a property you and tess
provide together.

The mechanism, not just the claim:

- Planning groups operations into phases whose mutable chunk sets are
  **disjoint**, so no two concurrent callbacks touch the same chunk.
- Result reduction runs in **plan order** after the executor returns,
  never in completion order, and the first non-executed status wins
  regardless of which worker finished first.
- Dirty records land in per-operation partitions and are merged by the
  caller afterwards.
- Pre-validation makes runtime aborts unreachable, so serial and pooled
  runs cannot diverge on a partially applied plan.

What is actually proven: the colony scenario asserts serial-versus-pool
equality and worker-count invariance (2 and 4 workers) over final agent
positions, step counts, and topology answers, with the pool asserted to
have genuinely dispatched so the comparison cannot pass on an idle
task.

Scope limits:

- The invariance gate covers **one scenario** at worker counts 0, 2,
  and 4. It is strong evidence for the mechanism, not a proof for every
  workload.
- Determinism **across machines, compilers, or standard libraries is
  not claimed**. Counter goldens run in shadow mode and report drift;
  they do not gate.
- No floating-point reduction-order guarantee is made.
- Route-cache results depend on the configured staleness policy. The
  default (`UnitRouteStaleness::WholeWorldExact`) serves routes
  identical to fresh recomputation after any world change. The opt-in
  `ScopedFeasible` mode is deterministic but weaker: served routes are
  legal at their stated cost and were optimal when stored, and a
  cost-lowering edit outside a route's chunk footprint can leave it
  suboptimal until retirement. Enabling it is a semantics choice, not a
  tuning knob.

## Thread-pool ownership

**tess never starts a thread on its own.** The only thread-creating
types are the executors you construct explicitly.

Ownership and lifetime:

- `WorkerPoolPhaseExecutor` starts its threads in the constructor and
  joins them in the destructor. It is neither copyable nor movable, so
  hold it by reference and let it outlive every dispatch that uses it.
- `ScopedThreadPhaseExecutor` spawns and joins per phase, and allocates
  on every call. It is the simple option, not the allocation-free one.

Constraints you must honour, specific to
`WorkerPoolPhaseExecutor` (`SerialPhaseExecutor` is stateless and
`ScopedThreadPhaseExecutor` keeps its dispatch state locally, so
neither carries these):

- At most one dispatch per pool executor may be in flight.
- A callback must not re-enter its own pool executor, and must not call
  `reserve_operations` on it during a dispatch. Both violations fail fast in
  debug and release builds while the pool mutex protects the state check,
  before shared dispatch state can be changed.
- **Callbacks are shared across pool workers.** The kernel you supply
  must be stateless or self-synchronising.
- `Schedule::notify_dirty`, `notify_events`, and `request_run` are
  frame-owner-thread calls. They are reentrant from task bodies, but
  never from a pool worker.

You may supply your own executor: anything satisfying the
`PhaseExecutor` concept works. A concurrent executor must not declare
`serial_execution_tag`.

The worker-pool and scoped-thread executors are stable interfaces. The scoped
executor remains the simple per-dispatch thread-owning alternative; it is not
an asynchronous scheduler.

## Steady-state allocations

tess does **not** claim to be allocation-free. It claims that specific
operations do not allocate **once warm and reserved**, and it tests
that claim.

Proven allocation-free in steady state, after the matching reserve —
stated at the granularity the tests actually cover:

- Worker-pool and serial dispatch at the executor level, and the
  queued-operation wrapper under the serial executor. There is no
  allocation-counted test of the queued wrapper *on a pool*, so treat
  that combination as untested rather than proven.
- Queued-operation planning through the reuse overload.
- Schedule ticks.
- Render-delta collection.
- Warmed **movement-only** ECS ticks, for the generic, EnTT, and Flecs
  adapters. Those tests assert that path processing did not occur, so
  this is not a claim about ticks that plan paths.

Four benchmarks additionally abort if a steady-state iteration
allocates, so a regression fails rather than quietly costing
allocations: path A*, EnTT ECS, fields, and render delta.

The conditions are the contract:

- Every claim above depends on having reserved for the working set,
  and the reservations are more than one number: operations for the
  pool, capacity for delta frames, maximum population for ECS batches,
  and — for field products — goal, node, dependency, and scratch
  capacity, not merely chunk count. Under-reserving any one of them
  reintroduces allocation.
- A phase larger than the reserved size reallocates on that dispatch.
- Caches differ, and the difference matters. A cache backed by a
  reusable reserved arena stops allocating once warm; the field-product
  cache allocates a fresh heap-owned product for **every new-key
  store**, before it evicts to budget. A full cache under a churning
  key set therefore keeps allocating on every miss, indefinitely.
- First touch and growth allocate. There is no global "never allocates
  after warmup" property, and claiming one would be false.

## The experimental namespace

`include/tess/experimental/` holds work whose shape is still being
validated. Names there may change or be removed in any release, including
a patch release, and they are excluded from whatever compatibility promise
a future 1.0 makes. The stable maintenance contract — tasks, budgets,
metrics, opaque handles, explicit results, the structural backend boundary,
the registered scheduler, synchronous immediate execution, and the external
chunk adapter — lives under `tess::maintenance` and is covered by
[support](support.md). What remains experimental is the deferred
maintenance machinery: the virtual `MaintenanceScheduler` interface and the
`DirtyBitScheduler`, `FifoScheduler`, and `CoalescingScheduler` backends.
Supplying one of those types to the stable facade explicitly does not make
it stable.

The template behind the queued experimental aliases,
`detail::QueuedScheduler<Coalescing>`, is not a supported name: it
lives in `detail`, which `docs/style.md` excludes from source compatibility,
and it is spelled that way deliberately so an alias can be repointed without
breaking callers who used the documented name.

## Residency coverage

Four families are dense-only. They `static_assert` on `AlwaysResident`
and do not compile against a `SparseResidentWorld`:

- the queued-operations layer (`ops/queued.h`)
- the weighted distance-field product family
  (`path/field_product_cache.h`)
- the portal-route product family (`path/portal_route.h`,
  `path/portal_segment_cache.h`)
- the PIBT tier (`sim/pibt_movement.h`)

This is a residency coverage boundary, not an experimental marker. These
families are production-promoted and tested on dense worlds; what is
missing is the sparse dimension. They stay in the ordinary public
namespace rather than moving to `include/tess/experimental/`, because
relocating them would churn every consumer include to signal a maturity
difference that is not the actual distinction.

The header manifest, not directory placement alone, defines stability; see
[support](support.md). The current dense queued-operation, field-product, and
PIBT signatures are frozen for 1.x. Sparse residency support must use distinct
entry points or additive overloads that leave those dense signatures and their
existing call resolution unchanged. Such variants can accept the
`MissingChunkPolicy` used by sparse-aware path entry points such as
`astar_path` and `cached_astar_path`.

Note that `weighted_path_batch` is residency-generic and *does* compile
against a sparse world, but currently hardcodes `AssumeImpassable`: it
answers `NoPath` across a missing chunk rather than `Indeterminate`.
Adding a policy-aware variant there is open work and remains subject to the
additive-entry-point rule above.

Everything else in the public surface is residency-generic: it either
works on both world kinds, or names the difference in its own
documentation.

## Platforms and compilers

tess requires a C++20 compiler. CMake-based source and installed-package
integration additionally require CMake 3.25 or newer; the portable headers
release asset requires only an include-path-capable build. Beyond that, support
means *continuously tested*. Ordinary change CI and exact-SHA release CI
provide two layers of evidence:

- Ubuntu 24.04 with Clang builds and runs the full tests, sanitizers, installed
  and FetchContent consumers, standalone headers, and macro configurations.
- Ubuntu 24.04 with GCC 12 and 14 performs warning-clean builds and full
  runtime tests on code changes.
- macOS 15 with AppleClang builds, tests, sanitizes, and consumes an install on
  non-PR full-tier runs. Release runs select Xcode 16.0 and add no-RTTI runtime
  evidence.
- Windows 2025 with current MSVC is a required PR build-and-test gate. Release
  runs use Windows 2022 to verify Visual Studio 2022 17.14/MSVC 19.44 and add
  no-RTTI runtime evidence.
- Exact-SHA release mode packages the canonical installed headers, directly
  compiles the extracted tar and zip assets with Clang 16, GCC 12, and MSVC
  19.44 without consumer-side CMake, and retains those tested bytes.

Consequences worth knowing before you depend on a platform:

- A macOS regression can merge and only surface on the next main,
  scheduled, or dispatched run.
- Benchmarks and coverage run on Ubuntu only.
- Every platform job is skipped for documentation-only changes, and
  pull-request thread sanitizer coverage is path-filtered rather than
  universal.

The tested compiler floors are GCC 12, Clang 16, AppleClang from Xcode 16.0,
and MSVC 19.44. CMake 3.25.3 is the separately tested floor for CMake-based
integration. These are evidence-backed support floors, not configure-time
rejection rules: a different C++20 toolchain may work, but is outside the
tested contract. See [support](support.md) for the complete floor and no-RTTI
policy.

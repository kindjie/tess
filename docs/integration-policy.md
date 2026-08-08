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

Virtual functions appear only in the experimental maintenance layer.
Virtuals need vtables, not RTTI.

Caveats stated plainly:

- **`-fno-rtti` is not tested.** No CI job builds that way. It is
  expected to work; it is not verified.
- `tag_identity` is unique **within one binary**. Cross-shared-library
  identity is not addressed, so do not compare tokens across a DSO
  boundary.

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
  `reserve_operations` on it during a dispatch. Debug builds assert.
  Release builds **deadlock or race** — reserving mid-dispatch can
  reallocate the results storage while workers are writing through it,
  which is memory corruption, not merely a hang.
- **Callbacks are shared across pool workers.** The kernel you supply
  must be stateless or self-synchronising.
- `Schedule::notify_dirty`, `notify_events`, and `request_run` are
  frame-owner-thread calls. They are reentrant from task bodies, but
  never from a pool worker.

You may supply your own executor: anything satisfying the
`PhaseExecutor` concept works. A concurrent executor must not declare
`serial_execution_tag`.

Both pool executors are marked prototype in their headers. Treat their
interfaces as less settled than the rest of the library.

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

## Platforms and compilers

tess requires a C++20 compiler and CMake 3.25 or newer. Beyond that,
support means *continuously tested*, and the honest picture is uneven:

| Platform | Compiler | What runs |
| --- | --- | --- |
| Ubuntu 24.04 | Clang | Build, full test suite, sanitizers, install and FetchContent smokes, standalone-header and macro-configuration checks |
| Ubuntu 24.04 | GCC | **Compile only** — no test execution |
| macOS 15 | Clang | Build and tests on every non-PR full-tier event (main pushes, the weekly schedule, manual dispatches) — **never on pull requests** |
| Windows 2025 | MSVC | Build and tests, required on pull requests |

Consequences worth knowing before you depend on a platform:

- A GCC-specific runtime bug would not be caught: GCC is a
  compile-only gate.
- A macOS regression can merge and only surface on the next main,
  scheduled, or dispatched run.
- Benchmarks and coverage run on Ubuntu only.
- Every platform job is skipped for documentation-only changes, and
  pull-request thread sanitizer coverage is path-filtered rather than
  universal.

**No minimum compiler version is enforced or claimed.** The build
requires C++20 through `target_compile_features`, and nothing rejects
an older compiler at configure time. That is deliberate: "the oldest
version we test" and "the oldest version that works" are different
claims, and a hard rejection would turn away compilers that work fine —
including consumers who add tess as a subproject and never use it.
Publishing a tested-version matrix with pinned floor jobs is the
honest form of that guarantee, and it is not built yet.

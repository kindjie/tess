# Integration policy

What tess guarantees to a consumer, and what it deliberately does not.
Every statement here is backed by code or a test; where the honest
answer is "not guaranteed" or "not tested", it says so rather than
leaving you to find out.

## Exceptions

tess **uses exceptions and cannot be built with `-fno-exceptions`.**
Headers contain `try`/`catch` blocks — rollback and join-then-rethrow
guards in the executors, schedule, topology, and auto-exec task — so
disabling exceptions is a compile error, not a degraded mode.

What throws:

- `std::length_error` when a bounded structure would exceed its
  capacity (the portal segment cache, planned dirty records).
- `std::bad_alloc` from allocation, including implicitly from any
  container growth.

What is guaranteed when something throws:

- A phase executor **joins every in-flight callback before
  propagating**, so no callback is still running when the exception
  reaches you.
- Queued-operation planning pre-validates, so a policy mismatch means
  nothing executes rather than a partially applied plan.
- Schedule tasks either side of a throwing task keep their triggers
  coherent.

What is **not** guaranteed:

- If several callbacks throw concurrently, **which** exception
  propagates is unspecified.
- There is no library-wide strong exception guarantee. Individual
  operations document their own rollback behaviour; absent that, assume
  the basic guarantee.
- Assertion failures do not throw. A failed `TESS_ASSERT` aborts, which
  is what keeps `noexcept` functions honest.

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

Identical inputs produce identical results whether operations run
serially or on a worker pool, and regardless of worker count.

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

## Thread-pool ownership

**tess never starts a thread on its own.** The only thread-creating
types are the executors you construct explicitly.

Ownership and lifetime:

- `WorkerPoolPhaseExecutor` starts its threads in the constructor and
  joins them in the destructor. It is neither copyable nor movable, so
  hold it by reference and let it outlive every dispatch that uses it.
- `ScopedThreadPhaseExecutor` spawns and joins per phase, and allocates
  on every call. It is the simple option, not the allocation-free one.

Constraints you must honour:

- At most one dispatch per executor may be in flight.
- A callback must not re-enter its own executor or reserve on it.
  Debug builds assert; release builds deadlock, exactly as documented.
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

Proven allocation-free in steady state, after the matching reserve:

- Worker-pool and serial dispatch of queued operations.
- Queued-operation planning through the reuse overload.
- Schedule ticks.
- Render-delta collection.
- ECS and EnTT ticks.

Three benchmarks additionally abort if a steady-state iteration
allocates, so a regression fails rather than quietly costing
allocations.

The conditions are the contract:

- Every claim above depends on having reserved for the working set:
  operations for the pool, capacity for delta frames, maximum
  population for ECS batches, chunk count for field products.
- A phase larger than the reserved size reallocates on that dispatch.
- Caches allocate on insert and eviction until they reach their
  capacity bound.
- First touch and growth allocate. There is no global "never allocates
  after warmup" property, and claiming one would be false.

## Platforms and compilers

tess requires a C++20 compiler and CMake 3.25 or newer. Beyond that,
support means *continuously tested*, and the honest picture is uneven:

| Platform | Compiler | What runs |
| --- | --- | --- |
| Ubuntu 24.04 | Clang | Build, full test suite, sanitizers, install and FetchContent smokes, standalone-header and macro-configuration checks |
| Ubuntu 24.04 | GCC | **Compile only** — no test execution |
| macOS 15 | Clang | Build and tests, but **not on pull requests** — only after merge |
| Windows 2025 | MSVC | Build and tests, required on pull requests |

Consequences worth knowing before you depend on a platform:

- A GCC-specific runtime bug would not be caught: GCC is a
  compile-only gate.
- A macOS regression can merge and only surface on the next main run.
- Benchmarks and coverage run on Ubuntu only.

**No minimum compiler version is enforced or claimed.** The build
requires C++20 through `target_compile_features`, and nothing rejects
an older compiler at configure time. That is deliberate: "the oldest
version we test" and "the oldest version that works" are different
claims, and a hard rejection would turn away compilers that work fine —
including consumers who add tess as a subproject and never use it.
Publishing a tested-version matrix with pinned floor jobs is the
honest form of that guarantee, and it is not built yet.

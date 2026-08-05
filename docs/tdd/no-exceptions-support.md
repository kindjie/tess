# TDD: Exception-Free Build Support

## 1. Status

Implemented. This document records historical design intent. The maintained
[exception-free architecture note](../architecture/no-exceptions.md) and
[integration policy](../integration-policy.md) describe the shipped contract.

## 2. Summary

Tess should support consumers that compile with language exceptions disabled.
The supported mode is intentionally pragmatic: successful operations retain
their normal behavior, but allocation failure, thread-creation failure, and an
exception thrown across a no-exception boundary are outside the supported
recovery contract.

Exception-enabled builds remain the default and preserve their existing source
behavior. Exception-free builds remove exception syntax and exception-only
coordination from every exception-free preprocessing path, including aggregate
headers. The checked APIs in Section 6.2 report deterministic, pre-allocation
capacity errors; legacy APIs fail fast when their existing throwing contract
cannot be represented.

The implementation should also expose no-throw execution paths that can reduce
code size and control-flow overhead in exception-enabled builds when callbacks
are statically known not to throw.

## 3. Context

The modern C++ design already calls for expected-style recoverable errors and
avoiding exceptions in hot paths. The implementation nevertheless uses
exceptions for capacity checks, rollback, callback propagation, and partial
worker-start failure.

A C++20 public-header probe at `c92accd` with exceptions disabled found:

- 41 of 64 public headers compile independently;
- 23 public headers fail directly or through a transitive include;
- nine library headers contain exception constructs; and
- narrow leaf-only consumers can already compile without exceptions, while
  ordinary aggregate-header consumers cannot.

The affected behavior is concentrated in block scratch allocation,
maintenance, operation execution, queued result delivery, portal segment
caching, simulation scheduling, automatic execution, and topology rebuilding.
The failure is therefore not a fundamental property of storage, pathfinding,
or field algorithms. It is a cross-cutting integration contract that can be
isolated.

## 4. Goals

- Compile every supported public header with language exceptions disabled.
- Support `tess/tess.h` and the other aggregate headers in that mode.
- Preserve successful-operation semantics and deterministic results.
- Preserve current exception-enabled source behavior by default.
- Give callers the checked paths in Section 6.2 for deterministic
  capacity-limit failures before attempting an allocation.
- Make Tess-detected capacity errors report a status or use one deterministic
  fail-fast primitive.
- Remove exception-only state and branches from no-throw execution paths.
- Reuse no-throw fast paths in exception-enabled builds when statically safe.
- Validate the mode in installed and source-tree consumer configurations.
- Keep profiling and crash-symbolization choices independent from exception
  support.
- Classify every potentially throwing standard-library operation used by an
  exception-free path.

## 5. Non-goals

- Recovering from general allocation failure.
- Recovering from operating-system thread creation failure.
- Catching exceptions thrown by application callbacks in an exception-free
  translation unit.
- Supporting mixed exception configurations within one program.
- Making every public function unconditionally `noexcept`.
- Replacing assertions or the existing invariant policy.
- Removing unwind tables, frame pointers, debug information, or other data
  needed by profilers and crash tooling.
- Establishing a stable binary ABI across compilers or build modes.
- Redesigning all existing result types around a new dependency.

## 6. Supported failure contract

Successful operations, explicit status-returning failures, compile-time
configuration errors, and internal invariant handling have equivalent behavior
in both modes.

The remaining failures behave as follows:

- Detectable size or arithmetic overflow preserves the existing exception in
  an exception-enabled build. A checked operation returns its result in both
  modes; a legacy throwing operation fails fast when exceptions are disabled.
- Allocation failure preserves existing propagation and rollback when
  exceptions are enabled. It is outside the exception-free contract, with no
  state or termination guarantee.
- Worker thread creation failure preserves existing propagation after joining
  started workers when exceptions are enabled. It is outside the
  exception-free contract; the standard library may terminate or propagate
  across exception-unaware frames before Tess can regain control.
- An application callback exception preserves its current propagation and
  rollback contract when exceptions are enabled. Throwing from a callback in
  an exception-free build is unsupported, has no Tess state guarantee, and may
  terminate or unwind according to the toolchain and surrounding code.

The same no-throw precondition applies to every application-supplied operation
that Tess invokes: callbacks, visitors, providers, kernels, predicates,
allocators, and value-type construction, assignment, move, copy, and
destruction. Enforce the rule statically where the public type retains enough
information; otherwise document it at the extension point.

The implementation must inventory potentially throwing standard-library
operations on exception-free paths. It must prevalidate deterministic failures
such as arithmetic overflow, invalid alternatives, or excessive sizes.
Resource and operating-system failures remain outside the supported contract.

### 6.1 Deterministic fail-fast behavior

Add one internal `[[noreturn]]` fail-fast primitive. It emits a diagnostic when
diagnostics are enabled and ends in `std::abort()`. Legacy wrappers use this
primitive for Tess-detected capacity errors when exceptions are disabled.
Assertions are not an acceptable substitute because release builds may remove
them.

The first version does not install a process-global `std::new_handler` or a
terminate handler. An application that requires deterministic out-of-memory
termination owns that process-wide policy.

### 6.2 Normative checked operations

Checked capacity APIs only detect errors that can be established before an
allocation, such as arithmetic overflow or exceeding a container's maximum
size. They do not promise to convert allocator failure into a recoverable
result.

Phase A covers this complete initial set:

- `BlockScratch::reserve_bytes_checked` returns `ReserveStatus::Reserved` or
  `ReserveStatus::CapacityExceeded`. Capacity failure leaves the object
  unchanged.
- `WeightedPortalSegmentCache::reserve_segments_checked` and
  `reserve_path_nodes_checked` return the same `ReserveStatus` values.
- `WeightedPortalSegmentCache::ClassView::store_checked` returns
  `PortalSegmentStoreStatus::Completed` or `CapacityExceeded`. `Completed`
  includes the existing intentional no-store cases. Capacity failure leaves
  live entries, paths, and counters unchanged.
- `collect_planned_dirty` appends
  `PlannedDirtyCollectStatus::CapacityExceeded`; source partitions and the
  destination accumulator remain unchanged for that result.
- Both partition-collecting `merge_planned_dirty` overloads append
  `PlannedDirtyMergeStatus::CapacityExceeded`; the world and source partitions
  remain unchanged, while caller-provided scratch may be reset.

The names above are part of this design. Additions require a design amendment
or a documented implementation divergence. The checked operations can still
propagate allocation failure in an exception-enabled build; `checked` refers
only to deterministic capacity validation.

Where an existing function returns `void` and throws on a deterministic
capacity error, implementation should add a checked sibling rather than
silently changing the exception-enabled contract. The existing function may
delegate to the checked operation and:

- preserve the existing exception in an exception-enabled build; or
- fail fast in an exception-free build.

Result-enum additions must be appended so existing numeric values remain
stable. A standard-library-like expected type should not be introduced solely
for this work; the C++20-compatible result patterns already used by the library
are sufficient.

## 7. Configuration and detection

Add a public core configuration header that derives exception availability
from compiler feature macros. The supported detection set includes
`__cpp_exceptions`, `__EXCEPTIONS`, and `_CPPUNWIND` as appropriate for the
active compiler.

The header exposes a non-overridable `TESS_HAS_EXCEPTIONS` macro and a
corresponding `constexpr` value. They reflect compiler capability; there is no
public switch that forces Tess down a different path while the compiler mode
stays unchanged. Tests that need the exception-free implementation compile
with the real toolchain flag.

Because Tess is header-only, compiler detection in CMake is not sufficient.
The installed library target remains neutral and does not force a consumer's
exception policy. Repository presets and CI targets apply `-fno-exceptions` on
Clang-family and GCC validation targets.

Native MSVC has no equivalent mode: omitting `/EHsc`, `/EHs`, and `/EHa` leaves
partial C++ exception behavior, not the Clang/GCC contract. The initial
supported exception-free matrix is therefore Clang-family and GCC. Phase A
includes an MSVC portability spike that removes the repository's unconditional
`/EHsc` from the spike target and verifies `_CPPUNWIND`, standard-library
containers, mutexes, and threads. Native MSVC support requires a later explicit
decision and documented compiler/STL recipe; detection-only compilation is not
called supported operation. See the official [MSVC `/EH` documentation][msvc].

> Implementation note (2026-08-04): native MSVC was later promoted using
> `/EHs-c-` and `_HAS_EXCEPTIONS=0`, with public-header, runtime, installed,
> and FetchContent coverage in a focused CI job parallel to the full Windows
> job. The maintained contract is in `docs/architecture/no-exceptions.md`.

All translation units in a program must use the same Tess exception
configuration. Exception policy is intentionally encoded in executor policy
types. `WorkerPoolPhaseExecutor` becomes an alias for a policy-specialized
implementation, so the exception-free specialization may omit private state
without giving one class two macro-dependent definitions.

The two whole-program modes may have different private representation and
symbols; cross-mode binary compatibility is not promised. Mixed configuration
remains unsupported. Use a link mismatch diagnostic where the toolchain offers
one, such as MSVC `#pragma detect_mismatch`. A portable link-time canary is not
required because a header-only library cannot guarantee one, so build-system
integration must apply the compiler mode consistently to every translation
unit.

## 8. Callback contract

In an exception-free build, application extension points follow the no-throw
precondition in Section 6. The global build configuration selects the direct
path even when an ordinary callback type lacks an explicit `noexcept`
annotation; successful callbacks do not need source changes merely to compile.

For new template extension points, prefer a compile-time `noexcept` requirement
when it does not break ordinary exception-free use. Where an existing erased
callback type cannot encode `noexcept` without a source-breaking change,
document the precondition and keep the enabled-build signature stable.

The enabled-build optimization is a separate route. It selects the no-throw
path only for explicitly `noexcept` callbacks, preserves that property through
schedule and automatic-execution adapters, and uses a no-throw erased thunk or
dispatch policy at the worker boundary. `std::is_nothrow_invocable_v` at the
outer template alone is insufficient. Tests must prove the property survives
each adapter layer.

## 9. Component design

### 9.1 Explicit capacity failures

Block scratch reservation, portal segment cache reservation and storage, and
dirty-record collection currently contain explicit or standard-library throws
for arithmetic or size overflow. Factor their checks into non-throwing helpers
that return a result before any allocation is attempted.

Cache sweeping with no pending insertion derives its required sizes from valid
live storage, so deterministic overflow is an invariant violation rather than a
recoverable public result. It shares the validated compaction helper but does
not add a checked sibling solely for this work.

Checked public operations should propagate that result. Existing throwing
wrappers retain their behavior when exceptions are enabled and fail fast when
they are disabled.

### 9.2 Phase execution

The exception-enabled phase executors retain callback exception capture,
worker cancellation, joining, and rethrow behavior.

The policy-specialized no-throw executor omits `std::exception_ptr`, the
exception mutex, and state used only to cancel peers after an exception. It
must still preserve ordinary synchronization, joins, deterministic result
collection, and status-based failures. Resource failure has no exception-free
cleanup guarantee because it is outside the supported contract.

The same path should be available to an exception-enabled build when all
invoked work is statically no-throw.

### 9.3 Scheduling and automatic execution

Schedule, automatic execution, result-channel delivery, and maintenance code
currently uses catches to restore transient state before rethrowing. Keep that
rollback path in exception-enabled builds.

Exception-free implementations perform the successful path directly. They
must retain rollback for explicit status failures. No postcondition is promised
after an unsupported thrown application operation or resource failure, and no
invariant may rely on the process necessarily terminating.

### 9.4 Topology

Topology construction retains strong rollback behavior when exceptions are
enabled. The exception-free path validates all deterministic input and size
conditions before mutation where practical, then performs construction without
catch syntax. Provider callbacks follow the no-throw callback contract;
resource failure has no exception-free state guarantee.

The design must not require a second public topology representation or a
duplicate exception-free facade.

### 9.5 Aggregate surfaces and examples

Conditional implementation belongs at the actual exception-dependent code,
not in aggregate headers. `tess/tess.h`, `tess/pathfinding.h`, and
`tess/simulation.h` should expose the same feature set in successful
exception-free operation.

Examples should avoid unconditional top-level catch blocks. Exception-enabled
examples may retain friendly error reporting behind the shared configuration;
exception-free example builds must exercise their actual bodies rather than
being reduced to include-only tests.

## 10. Compatibility

Exception-enabled builds are the compatibility baseline. Existing exceptions,
rollback guarantees, and public call patterns remain available there.

New checked capacity operations are additive. Existing APIs are not marked
`noexcept` merely because their exception-free implementation has a narrower
supported failure contract; doing so could alter overload resolution or
pointer types in enabled builds.

The exception-free configuration is a build-wide contract. It is not a new
runtime mode, and switching it does not require stored-data or serialization
format changes.

## 11. Performance design

The main expected gains are smaller generated code and simpler control flow.
Runtime gains are most plausible in callback and phase-dispatch machinery that
currently carries exception capture, exception-only synchronization,
cancellation checks, or rollback-only branches. Removing `try` and `catch`
alone is not expected to speed zero-cost exception implementations.

Core storage, field, and pathfinding loops should not be assumed to improve
merely because exceptions are disabled. Any runtime claim requires paired
measurement.

Compare three variants so compiler-wide effects are not attributed to Tess:

1. the normal exception-enabled baseline;
2. the exception-enabled end-to-end `noexcept` callback path; and
3. the compiler exception-free build.

Measure at least:

- executor and scheduler wall time with representative no-throw callbacks;
- the existing benchmark sentinels for storage, fields, and pathfinding;
- library consumer compile time, including an enabled consumer that instantiates
  both callback paths;
- object and executable `.text` size;
- exception and unwind metadata size, reported separately; and
- peak resident memory during compilation and representative runtime tests.

Do not couple this feature to disabling unwind tables. Profiler stacks and
useful crash traces may require unwind information even when C++ exception
handling is disabled.

Accepted, rejected, and deferred optimization experiments belong in the
optimization log during implementation. A no-throw fast path must not regress
an established benchmark sentinel by more than 5% without an explicit,
recorded tradeoff. Improvement claims require repeated paired runs under the
repository's benchmark policy.

## 12. Test strategy

Implementation follows test-first slices. Tests must distinguish compile
support from runtime coverage; an include-only pass is insufficient.

### 12.1 Compile contracts

- Compile every public header independently with exceptions disabled.
- Compile all aggregate headers with exceptions disabled.
- Compile representative installed-package and source-tree consumers.
- Run the existing forward/reverse include-order, macro-cell, and interface
  header-set checks in the exception-free preset.
- Cover diagnostics enabled and disabled, assertions enabled and disabled, an
  optimized `NDEBUG` build, and optional adapter header sets when their
  dependencies are present.
- Assert that `TESS_HAS_EXCEPTIONS` is false in every exception-free target.
- Add compile coverage for both ordinary exception-free callbacks and the
  explicitly `noexcept` enabled-build fast path.
- Keep a generated inventory so the initial 41/64 result and every later
  public-header result are reproducible.

### 12.2 Runtime contracts

- Run ordinary storage, block, pathfinding, topology, queue, scheduler,
  automatic-execution, and parallel-executor behavior without exceptions.
- Exercise existing explicit status failures and deterministic rollback.
- Add death tests for deterministic overflow through legacy fail-fast wrappers.
- Test checked capacity results without attempting an enormous allocation.
- Verify started workers are joined on all testable status-based exits.
- Exercise the enabled-build no-throw route through every adapter layer.
- Preserve and run the current exception and rollback tests in the enabled
  configuration.

Existing tests that contain exception syntax should be split or conditionally
compiled so the underlying successful and status-based behavior still runs in
the exception-free job. The job must not obtain a green result by excluding
entire affected subsystems.

Maintain a no-exception test manifest or CTest labels for each affected
subsystem and compare it with the enabled manifest in CI. Run fail-fast death
tests separately from threaded sanitizer jobs when combining them would make
the result flaky.

### 12.3 Toolchain matrix

Validate Clang-family and GCC exception-free configurations. At least one job
runs warnings-as-errors and sanitizers. The normal exception-enabled matrix
remains the primary compatibility signal. Run the native MSVC portability
spike separately and do not label it supported exception-free operation until
the decision in Section 7 is satisfied.

## 13. Delivery plan

### Phase A: contract and primitives

1. Add compiler feature detection and configuration tests.
2. Add the standard-library throwing-operation inventory.
3. Add the normative checked-capacity operations and tests.
4. Preserve existing enabled-build exceptions through thin wrappers.
5. Complete the native MSVC portability spike and record its decision.

### Phase B: execution paths

1. Add the policy-specialized no-throw phase executor under tests.
2. Remove exception-only state from that specialization.
3. Add the end-to-end enabled-build `noexcept` thunk and adapter path.
4. Adapt schedule, automatic execution, result channels, topology, and
   maintenance to the shared contract.

### Phase C: complete public surface

1. Make every standalone and aggregate public header compile.
2. Convert examples and consumer contract tests.
3. Add install-tree coverage and the cross-toolchain CI matrix.

### Phase D: measure and document

1. Run paired runtime, compile-time, code-size, and peak-memory measurements.
2. Record all optimization experiments in the optimization log.
3. Update the maintained integration policy, architecture documentation, and
   design changelog to describe the shipped behavior.
4. Publish exact supported compiler flags and application failure-policy
   guidance.

Each phase should be independently reviewable. Configuration and correctness
must land before performance claims.

## 14. Acceptance criteria

- All supported public and aggregate headers compile with exceptions disabled.
- Representative library functionality runs in that configuration rather than
  being compiled out.
- Exception-enabled behavior and tests remain unchanged by default.
- Deterministic capacity errors have a tested checked path where specified.
- Unrecoverable resource failures and throwing callbacks are documented as
  outside the contract, not silently reported as recoverable or guaranteed to
  terminate.
- Executor representation differences are encoded in policy-specialized types,
  not macro-dependent definitions of one class.
- Installed and source-tree consumers are covered.
- Supported Clang-family and GCC CI contain a green exception-free job.
- Existing performance sentinels stay within the accepted regression budget.
- Code-size, compile-time, runtime, and peak-memory results are recorded.
- Maintained documentation is updated when implementation ships.

## 15. Rejected alternatives

### Support only a leaf-header subset

This describes today's accidental behavior and leaves ordinary consumers
unable to use the library's aggregate surfaces.

### Add duplicate exception-free facades

Parallel public APIs would drift, increase test cost, and prevent optimizations
from benefiting normal builds with no-throw callbacks.

### Replace every throw with unconditional termination

This would compile, but it would discard useful deterministic capacity results
and leave exception-only control flow in performance-sensitive components.

### Provide full resource-exhaustion recovery

Reliable recovery from allocator and thread exhaustion requires broader
application policy, reserve management, and often custom allocators. It is not
required for the intended consumers and would substantially expand the design.

### Disable unwind information with exceptions

Exception handling and stack unwinding metadata are related compiler concerns,
not the same product requirement. Removing profiler and crash-reporting support
is an unacceptable implicit side effect.

## 16. Risks and mitigations

- **ODR mismatches:** derive one non-overridable setting, encode executor
  representation in policy types, use toolchain mismatch diagnostics where
  available, and document the build-wide requirement.
- **False confidence from compile-only tests:** run real subsystem behavior in
  the exception-free matrix.
- **Callback failure surprises:** use compile-time diagnostics where practical;
  document that violations have no state or termination guarantee.
- **Hidden exception syntax in templates:** compile every public header and
  instantiate representative aggregate operations.
- **Hidden standard-library failures:** inventory throwing operations and
  prevalidate deterministic failure modes before mutation.
- **Behavior drift between paths:** share deterministic validation and core
  algorithms; isolate only error transport and rollback machinery.
- **Misleading speed claims:** require paired benchmarks and report code size,
  compile time, runtime, and memory as separate outcomes.

[msvc]:
  <https://learn.microsoft.com/cpp/build/reference/eh-exception-handling-model>

# Exception-Free Builds

Tess supports Clang-family and GCC consumers compiled with
`-fno-exceptions`, and native MSVC consumers compiled with `/EHs-c-` and
`_HAS_EXCEPTIONS=0`. The installed `tess::tess` target remains neutral: the
consumer applies the recipe consistently to every translation unit in the
program. `TESS_HAS_EXCEPTIONS` and `tess::has_exceptions` report the compiler
mode and cannot be overridden by an application definition.

Exception-enabled builds remain the default. Their callback propagation,
worker joining, and operation-specific rollback behavior are unchanged.

## Failure Contract

Successful operations and explicit status results behave the same in both
modes. Deterministic capacity checks reach non-throwing entry points two
different ways, and the distinction matters to an exception-enabled caller.

A new `_checked` entry point was added beside the existing throwing one,
whose exception-enabled behavior is unchanged:

- `BlockScratch::reserve_bytes_checked` beside `reserve_bytes`;
- `WeightedPortalSegmentCache::reserve_segments_checked` and
  `reserve_path_nodes_checked` beside `reserve_segments` and
  `reserve_path_nodes`; and
- `WeightedPortalSegmentCache::ClassView::store_checked` beside `store`.

The function itself stopped throwing, in **both** modes, and now reports
through a status. There is no throwing wrapper for these:

- `collect_planned_dirty`, which returned by throwing `std::length_error`
  and now returns `PlannedDirtyCollectStatus::CapacityExceeded`; and
- both partition-collecting `merge_planned_dirty` overloads, which likewise
  now return `PlannedDirtyMergeStatus::CapacityExceeded`.

An exception-enabled caller that upgraded across that change must check the
returned status rather than rely on `catch`. `AutoExecTask` does this
internally: it treats `CapacityExceeded` as a signal to run the
allocation-free fallback merge, which publishes every started callback's
dirty metadata, so the task's observable result is unchanged and the
condition does not escape `run()`.

`ReserveStatus`, `PortalSegmentStoreStatus`,
`PlannedDirtyCollectStatus`, and `PlannedDirtyMergeStatus` report capacity
failure before the associated storage allocation, before any world mutation,
and before the source records are consumed — so a rejected operation can
still be retried or handled from its inputs. The throwing entry points in
the first list keep their exception-enabled behavior and abort through one
internal fail-fast path when a deterministic capacity error occurs in an
exception-free build.

Two details qualify that, and neither costs a caller its inputs:

- Caller-supplied scratch is not preserved. The `PlannedDirtyPartitions`
  overload of `merge_planned_dirty` clears the accumulator it is handed
  before collecting, so that accumulator is empty on a `CapacityExceeded`
  return. The partitions themselves are untouched, which is what lets the
  allocation-free fallback merge publish them afterwards.
- A portal-segment store already at its segment budget is bounded by how
  many entries survive compaction, which is only known after a full
  dependency-validity sweep. It rejects in constant time where it can, and
  otherwise captures the candidate entry's dependencies before the
  compaction returns the status. That capture appends to an unreserved
  vector, so a path crossing several chunks may reallocate more than once.
  Cache storage is still untouched, and general allocation failure is
  outside the contract either way.

General allocation failure, operating-system thread-creation failure, and an
application operation that throws across an exception-free boundary are
outside the supported recovery contract. This applies to callbacks, visitors,
providers, kernels, predicates, allocators, and throwing value-type
operations. Tess does not install a process-wide new or terminate handler.

## Execution Policy

`WorkerPoolPhaseExecutor` and `ScopedThreadPhaseExecutor` select a
policy-specialized implementation from the compiler mode. Their exception-free
specializations omit callback exception state and cancellation coordination.
The implementation types are `WorkerPoolPhaseExecutorImpl` and
`ScopedThreadPhaseExecutorImpl`; applications should normally use the aliases.
`NoThrowWorkerPoolPhaseExecutor` and `NoThrowScopedThreadPhaseExecutor` expose
the same direct path to exception-enabled programs with an explicit no-throw
contract. Those aliases reject callbacks that are not typed `noexcept` when
compiler exceptions are enabled; exception-free builds continue to accept
ordinary callback types because the compiler mode supplies the contract.

An explicitly `noexcept` callback keeps that property through queued phase,
result-channel, schedule, and automatic-execution adapters. Existing erased
callback signatures remain source compatible; `ScheduleNoThrowTaskFn` is the
raw erased signature for callers that want to preserve the stronger contract.
Adapter setup completes all potentially growing internal storage before
entering the no-throw dispatch region, and allocation tests pin that invariant.

All translation units in a program must use the same exception mode. Mixed
mode programs and cross-mode ABI compatibility are unsupported.
`tess::has_exceptions` is an `inline constexpr bool` and the public executor
aliases are derived from it, so a mixed-mode program violates the
one-definition rule.

Detection of that mistake is uneven, and the build system carries the burden
on every toolchain:

- GCC and Clang have **no** mechanism to detect it. A mixed-mode program
  links silently.
- MSVC gets a partial link-time check: `tess/core/config.h` emits
  `#pragma detect_mismatch("tess_exception_mode", ...)` keyed on
  `_CPPUNWIND`, and `tess/core/capacity.h` does the same for the internal
  capacity-testing hook. Both only stamp translation units that include a
  Tess header, and neither observes the separate `_HAS_EXCEPTIONS` STL
  switch, so a `/EHsc` unit built with `_HAS_EXCEPTIONS=0` still passes.

## Potentially Throwing Standard-Library Operations

The exception-free path uses the standard library normally. Deterministic
size failures are prevalidated where Tess exposes a checked operation;
resource failures remain outside the contract.

| Component | Operations on the path | Deterministic validation | Residual failure |
| --- | --- | --- | --- |
| Block scratch | `make_unique_for_overwrite` | rounded byte count and overflow before growth | allocation failure |
| Portal segment cache | vector `reserve`, insertion, dependency capture, and compaction | entry/path additions and compaction totals before live mutation | allocation or throwing element/provider operation |
| Queued dirty collection | vector `reserve`, insertion, and `sort` | aggregate record count before source consumption | allocation during reserve |
| Scoped and pooled executors | vector growth, `thread` construction, mutexes, and condition variables | operation ranges and status results | allocation, thread, or synchronization resource failure |
| Schedule and auto-exec | task/result vector growth and erased callback invocation | existing cadence, plan, and status validation | allocation or application callback failure |
| Maintenance and topology | container growth, provider calls, moves, and swaps | existing bounds, identity, shape, and capacity checks before practical mutation points | allocation or throwing provider/value operation |
| Storage and path algorithms | vector growth and caller-supplied policy operations | existing coordinate, shape, arithmetic, and explicit status checks | allocation or throwing caller operation |

The build does not disable unwind tables, frame pointers, debug information,
or profiler support. Those choices are independent of C++ language exception
handling.

## Toolchains

CI compiles and runs the mode with Clang plus ASan/UBSan, GCC under
warnings-as-errors, and native MSVC. It builds standalone headers, macro
configurations, representative runtime behavior, an installed consumer, and
a FetchContent consumer in each supported compiler family. Complete examples
are additionally built and run with Clang or GCC.

AppleClang uses the Clang-family `-fno-exceptions` recipe, but CI does not
duplicate the exception-free contract suite on macOS. Linux Clang supplies
that language-mode coverage while ordinary macOS CI remains exception-enabled.
This intentionally contains CI time at the cost of not detecting an
AppleClang-specific exception-free regression in the repository matrix.

The exception-free contract targets are opt-in so ordinary developer and CI
build-all invocations do not compile both language modes. Configure them with
`-DTESS_BUILD_NO_EXCEPTIONS_TESTING=ON`; the normal build then includes the
exception-free runtime, consumer-contract, and macro-cell executables, so an
unfiltered `ctest` run has every registered executable. The standalone-header
verifier remains an explicit target because it registers no test. Focused
builds may select the relevant `tess_no_exceptions_*` targets before running
tests labeled `config:noexceptions`. CI keeps the focused MSVC contracts in a
job parallel to the existing full Windows build so this coverage does not
extend the critical path.

Native MSVC's `/EHs-c-` mode is not identical to `-fno-exceptions`: it does
not provide the same compile-time enforcement or safe recovery if an
exception is nevertheless thrown. Tess supports it as an exception-free by
construction configuration. The no-throw application-operation and resource
failure preconditions above are therefore especially important on MSVC.
Because `/EHs-c-` and `_HAS_EXCEPTIONS=0` are separate switches, the
`detect_mismatch` check described above does not cover the STL half of the
recipe, so build-system consistency is still required to avoid an
unsupported mixed-mode program.

`_HAS_EXCEPTIONS=0` is an MSVC STL implementation switch rather than a
supported public compiler mode. Microsoft STL maintainers have described it
as [largely untested, undocumented, and unsupported][msvc-stl-no-exceptions].
Tess therefore treats native MSVC support as version-sensitive, pins it with
CI, and does not promise recovery from standard-library failures.

[msvc-stl-no-exceptions]:
  <https://github.com/microsoft/STL/issues/2216#issuecomment-930561988>

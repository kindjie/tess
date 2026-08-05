# Exception-Free Builds

Tess supports Clang-family and GCC consumers compiled with
`-fno-exceptions`. The installed `tess::tess` target remains neutral: the
consumer applies the flag consistently to every translation unit in the
program. `TESS_HAS_EXCEPTIONS` and `tess::has_exceptions` report the compiler
mode and cannot be overridden by an application definition.

Exception-enabled builds remain the default. Their callback propagation,
worker joining, and operation-specific rollback behavior are unchanged.

## Failure Contract

Successful operations and explicit status results behave the same in both
modes. The following deterministic capacity checks have non-throwing entry
points:

- `BlockScratch::reserve_bytes_checked`;
- `WeightedPortalSegmentCache::reserve_segments_checked` and
  `reserve_path_nodes_checked`;
- `WeightedPortalSegmentCache::ClassView::store_checked`;
- `collect_planned_dirty`; and
- both partition-collecting `merge_planned_dirty` overloads.

`ReserveStatus`, `PortalSegmentStoreStatus`,
`PlannedDirtyCollectStatus`, and `PlannedDirtyMergeStatus` report capacity
failure before the associated allocation or live-state mutation. Existing
throwing wrappers preserve their exception-enabled behavior and abort through
one internal fail-fast path when a deterministic capacity error occurs in an
exception-free build.

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

CI compiles and runs the mode with Clang plus ASan/UBSan and with GCC under
warnings-as-errors. It also builds standalone headers, macro configurations,
complete examples, an installed consumer, and a FetchContent consumer.

Native MSVC is not a supported exception-free configuration. A separate
portability spike omits all `/EH` options and verifies feature detection plus
basic vector, mutex, and thread use. That is detection evidence only: MSVC's
partial behavior without `/EHsc`, `/EHs`, or `/EHa` is not equivalent to the
Clang/GCC contract.

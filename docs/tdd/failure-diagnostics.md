# Failure diagnostics and implementation-state accuracy

Status: implemented after independent review. This document records historical
design intent; maintained architecture and integration-policy pages describe
the shipped contract.

## Problem

Tess already distinguishes recoverable failures, unchecked hot-path
preconditions, and compile-time configuration errors. A few public surfaces
violate that policy in ways that hide the original mistake:

- an invalid `PathRequestRuntime` ticket becomes `PathStatus::NoPath` when
  assertions are disabled;
- some `Schedule` misuse becomes an idle-looking result, a silent no-op, or a
  task that receives a valid id but can never run;
- reentrant `ResumableWorkQueue` mutation becomes an ordinary `false`, empty
  result, or no-op, and invalid raw callback setup can survive until a later
  indirect call;
- flow-accounting owners can be rebound or overwritten while retaining work,
  breaking the accounting identity only in assertion-disabled builds; and
- several public templates fail through unnamed predicates, making compiler
  output state the implementation expression without explaining the required
  type or value.

Documentation has a related failure mode: current implementation state is
manually stated in several maintained and historical documents. The roadmap
is accurate, but some surrounding pages still describe superseded maintenance
outcomes or label implemented TDDs as proposed.

## Decision rule

The failure mechanism follows the boundary, not a blanket preference for
statuses, assertions, or termination.

| Condition | Mechanism |
| --- | --- |
| Expected data or environmental failure a caller can handle | Existing status, optional value, or checked entry point |
| Detectable uncertain ticket lookup | New checked lookup returning an optional result |
| Documented contract misuse whose current release behavior looks successful, corrupts state, or can continue unsafely | Unconditional `detail::fail_fast` with the API name, violated condition, and corrective action where practical |
| Unchecked coordinate, key, span, or local-id hot path with an existing checked alternative | Existing debug assertion; no new release-path branch |
| Invalid template argument visible at a public instantiation boundary | Named constraint where one already models the contract, otherwise an actionable `static_assert` message |
| Internal invariant unreachable from valid public inputs | Existing assertion unless continuing without it is independently unsafe |

This does not turn ordinary domain outcomes such as `NoPath`, capacity
rejection, missing sparse chunks, or archive mismatch into process failures.
It also does not make all preconditions recoverable.

## Runtime contract

### Path runtime tickets

`PathRequestRuntime::try_result(PathTicket)` returns
`std::optional<PathResult>`. It returns no value when the ticket generation is
stale, its index is out of range, or the latest processing pass did not finish.
A successful value has the same borrowed-path lifetime as `result()`.

Result publication is transactional. Starting any processing pass withdraws
the previous result span. Only after the pass finishes and
`refresh_result_spans()` has installed every borrowed path does the runtime
publish that batch. A throwing provider therefore leaves `results()` empty and
`try_result()` empty for every ticket, rather than exposing `Found` results
whose paths have not been installed or default-initialized `NoPath` slots.
Result-status, path-node, and per-pass strategy counters remain unpublished as
well; retained route and field-product cache counters still describe the cache
state actually left by the interrupted attempt.

`result()` retains its existing valid-input signature and `noexcept` contract,
but detectable invalid tickets fail in every build. Its message distinguishes
a stale generation, an out-of-range index, and an unpublished result batch,
and points uncertain callers to `try_result()`. It must never translate ticket
misuse into `NoPath`, because that is a reachability verdict.

`PathTicket` has no runtime identity. A ticket from another runtime whose index
and generation happen to match cannot be distinguished from a local ticket.
Originating-runtime provenance remains an explicit precondition on both lookup
functions. Expanding the ticket to carry an owner token would change its size,
copy semantics, and runtime lifecycle; that is deliberately outside this
additive diagnostics pass.

### Schedule lifecycle and task ids

The following are caller contract violations and fail in every build:

- `request_run()` with an unknown task id;
- registration or task-capacity reservation after `seal()`;
- registration with a null callback, `phase == SimPhase::Count` or a phase
  outside `[SimPhase::Input, SimPhase::Count)`, a cadence kind outside
  `[CadenceKind::EveryTick, CadenceKind::Manual]`, or a raw zero-valued
  `EveryN`/`Background` cadence;
- `run_tick()` before sealing or while another tick is active; and
- a background callback reporting more completed items than it was offered;
  and
- a non-background callback reporting nonzero background items.

The cadence factories continue to normalize a caller-supplied zero to one;
only a manually assembled invalid descriptor reaches the registration check.
Valid scheduling behavior and hot dispatch loops do not change.

### Resumable queue and flow ownership

Queue mutation during `advance()` is a contract violation, not an ordinary
rejection. `advance()`, `reserve_tickets()`, submission, terminal setters,
`clear()`, and `observe_flow_tick()` therefore fail unconditionally when
called reentrantly. Raw submission also rejects a null callback immediately,
and ticket-index exhaustion fails before narrowing.

`ResumableWorkQueue::set_flow_accounting()` and
`EventStream::set_flow_accounting()` fail when retained work exists. Event
stream reserve is setup-only and rejects every nonempty batch, preserving its
capacity contract and outstanding borrowed views. Assignment separately
rejects overwriting an attached stream with outstanding inventory. These
checks protect the already documented conservation identity; they do not add
synchronization or make one accountant safe to share between flows.

An async callback that reports `items_done` beyond its allowance continues to
settle that slot as `Failed`. The current debug assertion is removed so debug
and release builds implement the same documented outcome. That is an operation
outcome already represented by the queue rather than a container-lifecycle
violation.

## Compile-time diagnostics

Publicly reachable naked assertions receive messages that name both the
requirement and the relevant API. This focused first pass covers:

- `BlockScratch::allocate<T>` element and alignment requirements;
- `ResumableWorkQueue<T>` payload and continuation signatures;
- path runtime movement-class and positive bounded-cost requirements.

The broader audit also found unnamed assertions in queued-operation policies,
payload views, archive bounds, and residency-specific templates. Those are
valid follow-up candidates, but changing every message would obscure the
runtime safety contracts and documentation corrections in this PR.

The compiler remains the authority. A small supported-compiler compile-fail
fixture instantiates representative invalid public forms and checks only the
stable library-authored requirement substrings. The change does not add a
custom parser or attempt to print non-type template values that the compiler's
instantiation context already shows.

## Documentation-state corrections

Maintained architecture and policy pages state current behavior; TDDs retain
historical intent but carry an accurate status banner. This change:

- says the experimental maintenance header must be included explicitly;
- corrects only maintenance prose that still treats the rejected
  queued-coalescing experiment as the final implementation outcome;
- clarifies experimental scheduler aliases versus directly named scheduler
  classes where the current policy page omits them;
- states that the external world/storage maintenance adapter remains planned;
- marks blocked-agent recovery, bounded path-agent replanning, and
  exception-free support implemented in the TDD index and status banners.

No semantic documentation checker is introduced. The public-surface manifest
can prove symbol coverage, but it cannot infer whether prose such as “planned”
or “rejected” refers to a backend, an adapter, or a historical experiment.
Later state changes still require review of the linked maintained and
historical pages.

## Alternatives

- **Return new error enums from every misuse path.** Rejected: these are
  violated object-lifecycle or ownership contracts with no meaningful
  continuation, and adding branches to every valid call would imply recovery
  guarantees the surrounding object cannot provide.
- **Keep debug-only assertions.** Rejected where a release build currently
  manufactures a domain result, silently accepts the operation, or can damage
  retained state. That makes production diagnosis least useful where it is
  most needed.
- **Fail fast for every assertion.** Rejected: world coordinate access and
  other explicitly unchecked hot paths already have checked alternatives and
  deliberately compile their checks out.
- **Route contract violations through `WarningSink`.** Rejected: warnings are
  optional, currently caller-emitted, and cannot safely replace an
  authoritative result or prevent unsafe continuation.
- **Add streaming for every status enum.** Deferred as a separate ergonomics
  decision. It would broaden `tess/io.h` dependencies and does not correct the
  library-origin failures in scope here.

## Verification

- Test every unconditional failure in both the normal assertion-enabled cell
  and the existing `NDEBUG` cell, matching the stable diagnostic phrase.
- Test `try_result()` for valid, stale-generation, out-of-range,
  submitted-but-not-yet-processed, and interrupted-processing tickets, plus
  borrowed-path behavior. The interrupted pass publishes no partial span.
- Test that queue and event-stream misuse cannot change retained work or flow
  counters before failing.
- Keep compile-time predicates covered by positive and negative concept/type
  assertions; compile representative invalid fixtures and match stable
  library-authored diagnostic phrases on each supported compiler family.
- Let the existing documentation and TDD-index checks validate structure and
  links. Prose state corrections remain review-owned rather than being pinned
  with brittle substring tests.
- Add the release-facing changelog fragment and design-decision fragment
  required by the repository's maintained record streams.
- Run the affected unit targets with assertions enabled and disabled, then the
  repository's required pre-commit validation. CI remains authoritative for
  GCC, MSVC, exception-free, sanitizer, and full documentation coverage.

## Compatibility and limits

Valid programs retain behavior. Programs depending on a documented
precondition violation no longer receive a normal-looking fallback in release
builds. `try_result()` is additive. Foreign-runtime ticket misuse remains
undetectable by the current two-field ticket representation. The implementation
remains header-only and allocation-free on every fail-fast path before
termination.

This pass does not redesign batch span pairing, unchecked `PathView` access,
world coordinate/key access, structured archive mismatch detail, or the
warning record format. Those need separate checked-API or diagnostic-data
decisions rather than opportunistic message changes.

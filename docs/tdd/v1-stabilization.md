# TDD: 1.0 Stabilization and Compatibility

## 1. Status

Proposed. This document records the design and release gates for stabilizing
the pre-1.0 surface. It is not itself the 1.x compatibility contract. When the
plan is implemented, maintained documentation such as `docs/support.md`,
`docs/integration-policy.md`, architecture notes, public header comments, and
the CMake package configuration remain authoritative for shipped behavior.

Implementation divergences are recorded in the
[1.0 stabilization decision][stabilization-decision].

[stabilization-decision]:
  ../decisions/CHANGELOG.md#2026-08-11---define-the-enforceable-1x-stability-boundary

## 2. Summary

The v0.12 roadmap is feature-complete enough to support a 1.0 release. The
remaining work is not another capability milestone. It is the narrower task of
deciding which existing interfaces tess can preserve throughout 1.x, aligning
documentation and validation with that promise, and obtaining release-candidate
evidence before the promise becomes permanent.

Tess is a header-only template library. Its 1.x promise should therefore focus
on source compatibility, documented configuration and CMake contracts, and
archive-format compatibility. Stable binary ABI compatibility across tess
versions, compilers, standard libraries, build modes, or mixed-version
translation units is not promised.

The minimum blockers are:

- publish one internally consistent 1.x compatibility and support policy;
- classify and freeze the supported public surface while excluding
  experimental APIs;
- resolve the worker-pool prototype-versus-production contradiction;
- make CMake package version selection follow the 1.x compatibility policy;
- publish a continuously tested toolchain matrix that matches the claims; and
- pass the latest feature-frozen release candidate without a breaking
  correction.

Archive fuzzing, end-to-end package-manager validation, and external adoption
are strong release-candidate evidence. They should be completed where
practical, but an explicitly reviewed, evidence-backed deferral of one of those
items does not by itself make the 1.x compatibility promise dishonest.

## 3. Context

The maintained roadmap records every planned v0.5-v0.12 capability as
complete. The repository already gates installed and FetchContent consumers,
standalone headers, optional adapters, exception-free modes, sanitizers,
threaded execution, documentation, and calibrated benchmarks. Adding more
features would increase the surface that must be frozen without answering the
remaining stability questions.

The current pre-stable policy deliberately permits minor releases to change
public APIs and data layouts. It also describes CI results as compatibility
evidence rather than a permanent platform commitment. Those statements are
appropriate for 0.x and must be replaced, not merely supplemented, when 1.0
ships.

Three current inconsistencies require explicit decisions:

1. The roadmap describes the worker-pool executor as production machinery,
   while its headers and integration policy still call the parallel executors
   prototypes.
2. The all-in-one compatibility umbrella includes
   `experimental/maintenance.h`, even though experimental maintenance failed a
   promotion gate and is not part of the shipped storage model. This was the
   state when this historical plan was written; the later registered dirty-bit
   candidate passed its gates, while storage integration remains unshipped.
3. The installed CMake package uses `SameMinorVersion`, which prevents the
   normal 1.x source-compatible minor-version consumption that the stable
   policy is expected to promise.

The support matrix also needs a deliberate floor. GCC currently compiles but
does not execute the runtime suite, macOS is a post-merge full-tier check,
`-fno-rtti` is expected but untested, and minimum compiler versions are not
claimed. A 1.0 policy may keep a bounded matrix; it may not imply broader
support than the matrix continuously verifies.

## 4. Goals

- Define the exact compatibility promise that applies to stable 1.x releases.
- Classify every installed header as stable, optional stable, experimental, or
  implementation-only before the release candidate.
- Preserve a dependency-free CPU core and explicit optional integrations.
- Make experimental namespace and header exclusions visible to consumers and
  enforceable in repository checks.
- Promote the production worker pool with one unambiguous ownership and
  reentrancy contract.
- Publish tested compiler, standard-library, platform, CMake, exception, and
  RTTI configurations.
- Add proportional automated checks for the compatibility promise.
- Freeze the candidate surface before `v1.0.0-rc.1` and accept only compatible
  fixes between the release candidate and 1.0.
- Preserve the existing archive-v1 framing and explicit application migration
  boundary with released-fixture tests.
- Keep feature additions and unrelated roadmap extensions out of the critical
  path to 1.0.

## 5. Non-goals

- Stable binary ABI compatibility across tess releases or toolchains.
- Mixing different tess versions or configuration macros across translation
  units in one program.
- Making public C++ object layouts into a serialization format.
- Guaranteeing determinism across compilers, machines, standard libraries, or
  unspecified floating-point reductions.
- Adding flow fields, new editor machinery, new GPU algorithms, continuous
  steering, or externally acquired benchmark data.
- Requiring acceptance into a central package registry before 1.0.
- Requiring a fixed number of third-party users that the maintainer cannot
  schedule or control.
- Converting every documented precondition into a checked hot-path operation.

## 6. Stable compatibility contract

### 6.1 Source and configuration compatibility

The maintained 1.x policy must enumerate the stable headers and state that a
consumer using only their documented public API should continue to compile
after an upgrade within 1.x, subject to the published compiler and
configuration matrix.

The promise covers:

- stable names and documented signatures in the `tess` namespace;
- documented enum values, status semantics, and configuration macros;
- the `tess::tess` CMake target and installed package entry points;
- documented aggregate-header membership, except additive inclusions;
- supported exception and RTTI configurations; and
- documented archive inspection, compatibility, and migration results.

The promise does not cover `detail` or `internal` namespaces, undocumented
implementation types, private representation, compiler-generated symbols,
template instantiation names, or object layout. The policy must explicitly
state that a header-only library cannot support mixed tess definitions across
translation units without violating the C++ one-definition rule.

Before the release candidate, review the complete installed-header manifest.
Move implementation-only names behind an internal boundary where practical and
record any intentionally public low-level primitive. The resulting stable
header list becomes a maintained artifact rather than an inference from the
install file set.

### 6.2 Deprecation and exceptional corrections

A stable API may be deprecated in a 1.x minor release. Removal waits for the
next major version unless the exceptional correction process applies.
Deprecation notes must name the replacement and the earliest permitted major
release for removal.

Security fixes, removal of undefined behavior, and corrections to behavior
that contradicts the documented contract may require an exceptional change.
Such a change needs a decision-log entry, migration guidance, and the smallest
practical compatibility break. SemVer must not be used to preserve a known
memory-safety defect.

### 6.3 Archive compatibility

The archive byte envelope is separate from C++ object layout. Format v1's
magic, fixed header, checksum framing, scalar encoding, and status precedence
remain pinned by golden fixtures. Application schema identity and versioning
continue to make application-owned migration explicit.

The 1.x policy must specify which older tess-produced format-v1 fixtures a
current reader inspects and loads. CI should retain fixtures produced from the
release candidate and later stable releases so a compatible reader change
cannot silently strand persisted worlds. Unknown future formats remain an
explicit `UnsupportedFormat` boundary rather than an inferred migration.

## 7. Stable and experimental surface separation

The compatibility umbrella must stop including
`experimental/maintenance.h`. Consumers that evaluate maintenance prototypes
include that header explicitly. Everything under `tess::experimental` remains
outside the 1.x source-compatibility promise and may change or disappear in a
minor release after an explicit changelog notice.

The experimental exclusion is both structural and documented:

- stable aggregate headers do not include experimental headers;
- the installed package may still install experimental headers;
- the public-surface manifest labels experimental entries separately; and
- tests reject an experimental include added to a stable aggregate.

Promotion from experimental requires its own acceptance evidence, maintained
architecture update, decision entry, and addition to the stable manifest. A
namespace name alone is not sufficient separation if a compatibility umbrella
silently imports the API.

## 8. Worker-pool decision

`WorkerPoolPhaseExecutor` is promoted as stable production machinery. Remove
the prototype labels from its public comments and maintained documentation.
Keep its existing explicit contract:

- one dispatch per executor may be in flight;
- callbacks must not re-enter their own executor during dispatch;
- no caller may invoke `reserve_operations` while a dispatch is in flight;
- callback state must be immutable or synchronized; and
- the executor must outlive its dispatches and join its owned workers.

These are ordinary concurrency preconditions, comparable to requiring callers
not to race unsynchronized standard-library objects. The 1.0 plan does not add
a mandatory release-mode branch to every hot dispatch solely to diagnose
contract violations. Tests must continue to exercise debug misuse detection,
worker-count invariance for deterministic operation-local kernels, exception
joining, lifecycle reuse, and ThreadSanitizer scenarios.

The integration policy must describe the same production status as the
roadmap and headers. `ScopedThreadPhaseExecutor` remains the simple
spawn-per-phase alternative with its documented allocation behavior; it must
also have one non-prototype stability classification before the release
candidate.

## 9. Toolchain and configuration support

The 1.0 support page must publish exact continuously tested configurations,
including the oldest supported compiler major for each family. A configuration
outside that matrix may work, but is not a release-blocking support claim.

Minimum required changes:

- execute the GCC runtime suite rather than retaining a compile-only claim;
- keep full Clang, MSVC, and AppleClang build-and-test evidence for release
  commits;
- decide whether `-fno-rtti` is supported, adding a job if it is or an explicit
  exclusion if it is not;
- retain exception-enabled and supported exception-free consumer coverage;
- state that native MSVC exception-free mode relies on `_HAS_EXCEPTIONS=0`, an
  undocumented Microsoft STL switch, and is verified only against the named CI
  toolset; and
- validate new release-gating workflow paths with `workflow_dispatch` before
  depending on schedule- or main-only behavior.

macOS need not block every pull request if it remains a required full-tier
check on the exact release candidate and release commit. The published matrix
must describe that timing accurately.

## 10. Compatibility verification

At 1.0, the generated CMake package version file uses `SameMajorVersion` so a
consumer requesting a compatible 1.x package may select a later 1.x release.
Synthetic CMake tests must pin the selection behavior before the switch.

The release-candidate work adds:

- a representative consumer fixture written only against the stable manifest;
- a job that compiles the fixture against the candidate and, after 1.0, against
  later 1.x releases;
- archive-v1 fixtures produced by released tags;
- an automated check that rejects removal of stable headers and exported
  aggregate includes without the required major-version process; and
- checks that experimental headers cannot leak into stable aggregates.

An exhaustive C++ API-diff engine is not a prerequisite for 1.0. Template API
compatibility is not reducible to exported-symbol comparison. The maintained
manifest, representative consumers, archive fixtures, and review policy form
the initial enforceable contract and may be strengthened during 1.x.

## 11. Recommended release-candidate hardening

The following items are high-value evidence rather than unconditional semantic
blockers. Each should ship before 1.0 unless a reviewed decision records why it
is deferred and why the stable contract remains supportable.

### 11.1 Archive fuzzing

Add one coverage-guided fuzzing target for `inspect_world_archive` and typed
load preflight, seeded from canonical and malformed fixtures. Run it with
AddressSanitizer and UndefinedBehaviorSanitizer on the scheduled tier, retain
every discovered input as a regression corpus entry, and keep execution
network-free.

### 11.2 Package-manager recipes

Exercise one package-manager-native end-to-end workflow in CI. The existing
checkout-overlay vcpkg install is the initial candidate. The stable vcpkg
distribution recipe must fetch and hash a public release instead of referring
outside its port directory. Central package-registry acceptance is useful
distribution work but does not define library stability.

### 11.3 Downstream and soak evidence

Actively solicit release-candidate use in at least one substantial reference
consumer and, where available, independent downstreams. Record upgrade,
persistence, sparse residency, path invalidation, and worker-pool findings.
External adoption is an objective, not a numerical gate that can postpone 1.0
indefinitely.

### 11.4 Known benchmark-ceiling follow-up

The hosted runner confirmed the fields regression fix, but five fields-family
benchmarks still use deliberately broad bootstrap ceilings. A green result
under those ceilings is not strong evidence against another regression of the
same scale. Recalibrate the five fields bootstrap ceilings after ten post-fix
main-run baselines exist, following the maintained benchmark protocol. If the
required sample window is not available before the release candidate, record
that limitation as an explicit performance-gate deferral rather than describing
the broad ceilings as calibrated evidence.

## 12. Release sequence

### 12.1 v0.13.0: final breaking-change window

- Publish the draft 1.x compatibility and support policies.
- Complete the installed-header classification and reduce the stable surface.
- Remove experimental maintenance from `tess/tess.h`.
- Promote and consistently document the worker-pool contract.
- Establish the formal toolchain matrix and required CI changes.
- Add the `SameMajorVersion` selection tests while retaining the correct 0.x
  package behavior until the 1.0 version change.
- Recalibrate the five fields bootstrap ceilings if the ten-artifact post-fix
  window is available, or record the remaining evidence gap explicitly.
- Close the outstanding API-shape items from
  `docs/planning/audit-2026-08-07.md`, listed below.

#### 12.1.1 Outstanding API-shape items

The 2026-08-07 audit routed its API findings here rather than running them
as a separate workstream, but named none of them in this document, so the
remaining ones are recorded explicitly. Each entry states what is left
rather than which finding number is "done": several findings closed only
in part, and the remainders are cited against source below so the split is
checkable rather than remembered.

Source-breaking, and therefore this window or the next major version —
§12.2 freezes the surface at the first release candidate, and §6.1 and
§6.2 keep documented signatures source-compatible through 1.x:

- **API7.** Five `(start, goal)` `Coord3` parameter pairs across nine
  declaration sites collapse onto `PathRequest`. The finding also names
  three same-class hazards that are still present and belong with it:
  `set_caps(max_entries, max_path_nodes)`, the interconvertible defaulted
  `(max_steps, movement_dirty_mask)` pairs, and the GPU descriptors
  re-spelling `GpuProductHandle` as two loose `uint64_t`. The widest
  call-site sweep left; worth its own change rather than folding into
  another.
- **API11 (argument order).** The remaining half of API11, after the
  `noexcept` documentation landed. Every affected parameter pair is
  cross-type, so a stale call site is a compile error rather than a
  silent behaviour change — which is also the argument for dropping it:
  the cost is borne by consumers and the benefit is symmetry.
- **API17 (duplicate names).** Never dispositioned, and still present:
  `Pipeline::to_frontier` (`block/pipeline.h:217,297`) aliases
  `collect_into`, and
  `TraceBuffer::stats(category)` (`diagnostics/trace.h:101,209`) and
  `TimingSnapshot::category(category)` (`diagnostics/export.h:22,27`) do
  the same job under opposite names. Removing either alias breaks
  callers, so the choice — remove, or keep and document the pairing — has
  to be made in this window. The audit's third example,
  `EventStream::clear` versus `discard_all`, was already downgraded
  during consolidation: the header discloses the difference, making it
  redundant naming rather than a trap.
- **API3, remaining half.** `sim/delta_frame.h:153-159` says in the source
  that holding a `DeltaFrame` across `publish()` leaves its spans indexing
  cleared and refilled buffers, that this is documented rather than
  enforced, and that enforcement is tracked as the other half of API3. PRs
  #149 and #151 corrected the contract and made the collector move-only;
  both reviewers then argued against accessor-gating, so the decision to
  enforce or to accept the documented contract belongs in this window.
- **API10, remaining half.** The verification that retired this finding
  holds only for `AutoExecTask`. `ResumableWorkQueue` defines copy and
  move explicitly (`ops/async_work.h:116-149`), and `ResumableWorkTask` is
  implicitly copyable while holding a raw queue pointer
  (`sim/async_work_task.h:14-26`), so a copy silently aliases the queue.
  Decide whether those are intended before the surface freezes.
- **API13, remaining half.** The privatization closed in #161, but the
  1.x surface question did not: `docs/integration-policy.md:269-296`
  records that four dense-only families stay production-promoted in the
  ordinary public namespace and that adding sparse support will change
  their signatures. Either accept that break for 1.x or carve the
  families out before the promise starts.

Decision required before 1.0 even though the fix itself is additive:

- **API18.** `tag_identity` breaks silently across a DSO boundary — with
  `-fvisibility=hidden` a region graph built in a shared library reports
  `GraphStale` forever. The finding's instruction is to decide whether
  cross-DSO use is supported, not merely to document it, because the
  answer changes what the 1.0 promise covers. PR #167 put the caveat on
  one public type (`IntentPayloadView::holds`); the other affected types
  still carry nothing.

Additive and unconstrained by the window:

- **API8, remaining half.** Threading `MissingChunkPolicy` through
  `weighted_path_batch`, which still hardcodes `TreatAsBlocked` and
  answers `NoPath` across a missing chunk where the uncached call answers
  `Indeterminate`. The cached half closed in #144 after the batch attempt
  was withdrawn for breaking grouped goals;
  `docs/integration-policy.md:298-301` records the remainder as open work,
  and the fix needs per-member reclassification against the completed
  field.
- Adoption items **AD3-AD9** and **D7** (Doxygen coverage).

Closed as unfixed with written rationale, and not to be reopened as
pending work: **API9** (the premise holds, but the fix is a World-concept
redesign rather than a break) and the `ResumableWorkQueue` half of
**API12** (PR #167: `state(ticket)` and `result_version(ticket)` already
separate the causes the finding said were collapsed).

### 12.2 v1.0.0-rc.1: feature and surface freeze

- Set the candidate version and activate the proposed stable manifest.
- Permit only compatible fixes, documentation corrections, validation, and
  release engineering.
- Add candidate consumer and archive fixtures.
- Complete or explicitly defer the recommended fuzzing, package-manager, and
  downstream-evidence work.
- Close or explicitly defer every acknowledged ineffective performance gate,
  including any remaining fields-family bootstrap ceiling.
- Run the complete supported matrix on the exact candidate commit.

If a breaking correction is required, publish another release candidate and
restart the compatibility observation window. Do not hide a breaking change
between the last candidate and 1.0.

### 12.3 v1.0.0: compatibility commitment

Release only when:

- every mandatory contract and matrix item is implemented and documented;
- the latest release candidate needs no breaking correction;
- required CI, documentation, security analysis, sanitizers, package
  consumers, and calibrated benchmark gates are green on the exact release
  commit;
- open correctness, security, performance-gate, and release-process incidents
  are triaged; and
- the changelog and upgrade guide describe the stable promise and all
  pre-1.0 migrations.

## 13. Acceptance criteria

The stabilization plan is complete when all of the following are true:

1. Maintained support and compatibility documentation contains no conflicting
   pre-1.0 or prototype claims.
2. Every installed header has an explicit stability classification.
3. Stable aggregates contain no experimental includes.
4. Worker-pool headers, roadmap, architecture, and integration policy agree on
   one production contract.
5. CMake accepts compatible 1.x packages under `SameMajorVersion` and rejects
   incompatible major versions in tests.
6. The published toolchain matrix is continuously exercised at its claimed
   floors, including GCC runtime execution.
7. Stable consumer and archive fixtures pass against the release candidate.
8. No known untriaged correctness, security, performance-gate, or
   release-process failure remains open for the candidate.
9. The exact candidate commit passes every required release workflow.
10. No known breaking change is introduced between the final candidate and
    1.0; discovering one triggers the Section 12.2 restart rule.

## 14. Documentation and decision updates

Implementation slices update maintained sources in the same pull request:

- `docs/support.md` owns the current support and compatibility policy;
- `docs/integration-policy.md` owns build-mode and concurrency constraints;
- architecture notes own shipped subsystem behavior;
- `docs/roadmap.md` owns shipped, planned, and out-of-scope capability status;
- `docs/decisions/CHANGELOG.md` records promotions, exclusions, and deliberate
  deferrals; and
- public header comments own API-local preconditions.

This TDD remains historical intent. Later implementation divergence is
recorded in those maintained documents rather than rewriting the proposal into
an API reference.

Implementation note: the compatibility evidence boundary was narrowed from a
source-level declaration model to inventories plus compiled consumers. See the
[2026-08-12 compatibility evidence decision][compatibility-evidence].

[compatibility-evidence]:
  ../decisions/CHANGELOG.md#2026-08-12---keep-c-semantics-in-compiled-compatibility-evidence

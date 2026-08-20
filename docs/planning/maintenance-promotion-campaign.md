# Maintenance promotion campaign

Status: **Frozen MNT-3 protocol** (2026-08-19). This document and
`bench/maintenance-campaign.json` define the campaign before M3 or Steam Deck
candidate results are inspected. The execution plan owns the release decision;
this document owns only the reproducible evidence method.

## Authority and stop rules

The adapter correctness suite is the gate. Timing starts only after the exact
candidate passes the full normal suite plus adapter-focused ASan/UBSan and TSan
runs. A contract, sanitizer, allocation, archive, rescan, token, dirty-owner,
or residency-generation failure returns to MNT-2 and invalidates both device
legs.

The controlled campaign decides only whether `DirtyBitScheduler` graduates.
It cannot block graduation of the stable task, handle, result, adapter, and
immediate-execution contract after correctness passes. Hosted Linux maintenance
thresholds remain advisory; this one-off M3 and Steam Deck campaign does not
calibrate them.

The candidate's canonical path-result and missing-chunk policy changes are
covered by the exact-commit normal and sanitizer gates. They do not add a cell
to this adapter-only timing campaign. Canonical archive equivalence here is
explicitly world archive format v2; the archive remains an authoritative-state
oracle rather than evidence for dirty masks, strong content versions, residency
generations, or derived products that it intentionally omits.

The measured implementation is frozen by a build manifest that binds the clean
source commit, raw config and canonical config, benchmark and campaign-tool
sources, benchmark binary, compiler, sanitized compile and link commands, and
the resolved compile and link drivers, and the pinned SDK or native build
context. The binary also embeds the source,
config, benchmark-source, and tool digests; manifest creation and every
observation verify them from Google Benchmark context. Manifest creation reads
the same identity through a dedicated mode that runs no benchmark or adapter
code. A change to the adapter,
registered scheduler, backend, benchmark, fixture, compiler, effective flags,
or SDK invalidates both timing legs. A later
mechanical move may carry evidence forward only when MNT-4 records why the
measured binary and benchmark remain representative.

## Matrix and measurements

`tess_bench_maintenance_campaign` instantiates the real external adapter for
every cell. It uses a scan-derived product and checks the product against an
independent world rescan, its token, and owned dirty flags after timing.

The matrix covers total-pipeline dense, actual sparse-resident, and mixed edit
patterns; explicit flush and budgeted completion; and 16, 64, 256, 1,024, and
4,096 registered-task scaling. Every cell runs Immediate, FIFO,
queued-coalescing, and dirty-bit backends. FIFO capacity is fixed large enough
to retain every offer; any capacity failure aborts the cell. Recorded counters
include offers, rebuilds, drain calls, schedules, coalesced calls, executions,
capacity failures, scanned values, resident slots, the authoritative checksum,
active task count, and per-process peak RSS. Each scaling cell fails unless its
registered slots and active offers equal its declared 16, 64, 256, 1,024, or
4,096 counts. Allocation authority comes from the separate warmed
dense and sparse adapter tests under every backend so allocator instrumentation
does not distort timing.

Each device uses 30 blocks and a 50 ms minimum measurement per isolated cell.
Within a block the collector orders sorted workload/backend identities by a
SHA-256 rank of the frozen seed, repetition, and identity. The analyzer
reconstructs that schedule exactly. CPU time is the decision metric; wall time,
work counters, memory, stderr, device environment, source and binary identities,
and per-invocation Google Benchmark context remain explanatory evidence.
Hardware policy binds `m3` to the declared Apple system and CPU and
`steam-deck` to the declared SteamOS-class Linux machine and APU. Context drift
or enabled CPU frequency scaling fails closed. Campaigns serialize on each
physical device. Absolute timings are never compared between devices.

The config freezes the M3 calibration/candidate seeds as 130013/130031 and
the Steam Deck seeds as 130019/130037. The collector and analyzers reject a
different device label, seed, repetition count, or minimum measurement time.

## Calibration and decision

Before candidate comparison, each device runs a separate 30-block A/A pass of
the queued-coalescing control. For each workload, the device threshold is:

```text
relative = max(8%, 2 * A/A p95 absolute paired relative delta)
absolute = max(500 ns, 2 * A/A p95 absolute paired time delta)
```

An A/A p95 relative delta above 10% invalidates that device leg; the threshold
must not expand to excuse an unstable cell. Candidate comparisons use paired
per-block ratios and seeded 95% percentile-bootstrap intervals.

Queued coalescing is the predeclared primary comparator. The primary result is
the geometric mean of dense, sparse, and mixed total-pipeline cells. Immediate
and queued coalescing are non-regression guardrails for every cell; FIFO is the
amplification sanity control. Dirty bit promotes only when the aggregate
primary result materially wins on at least one device and both devices
demonstrate non-regression: every primary and guardrail interval must remain
inside the material-regression boundary. A regression or inconclusive interval
on either device keeps it experimental. A local win cannot override an
aggregate primary regression.

## Reproduction and retention

Stage the exact committed M3 harness into a new empty result directory. The
native stage rejects every tracked, untracked, or ignored worktree entry before
and after a fresh isolated campaign-target build outside the source tree,
creates the build manifest immediately, and retains an exact checksummed
binary/config/tool/benchmark-source/runner bundle. It rejects a changed tree,
incomplete compile/link provenance, or a binary whose embedded identities
differ:

```sh
mnt3_m3_results=/path/to/new-empty-m3-results
mkdir -p "$mnt3_m3_results"
[ -z "$(find "$mnt3_m3_results" -mindepth 1 -print -quit)" ]
tools/maintenance-campaign-native.sh stage "$mnt3_m3_results"
```

The native whole-phase runner reads the M3 repetition, minimum-time, and seed
values from `bench/maintenance-campaign.json`. It writes a complete phase
checksum even on failure and refuses calibration unless the build manifest is
part of the exact untouched stage bundle. Every phase also requires repository
`HEAD` to be the build manifest's source commit with no tracked changes.
Candidate collection requires the exact retained calibration inventory and
refuses every partial prior candidate attempt. Use a newly staged result
directory for every rerun.

```sh
tools/maintenance-campaign-native.sh calibration "$mnt3_m3_results"
tools/maintenance-campaign-native.sh candidate "$mnt3_m3_results"
```

The runner invokes `calibrate` then `thresholds` in its calibration phase.
`collect` and `analyze` both require the retained `calibration.json`; they
recompute the threshold manifest and require exact equality before accepting
it. `collect` also embeds the threshold and calibration digests. After both
device reports exist, run `decide` with exactly those reports and the frozen
config.

The Deck route is deliberately separate from the generic one-binary benchmark
helper. Stage and hash the complete cross-built bundle without contacting the
device, then run one whole pinned phase at a time:

```sh
tools/steamdeck/deck campaign stage <empty-bundle-dir>
tools/steamdeck/deck campaign run \
  <bundle-dir> calibration <new-run-id> <empty-results-dir>
tools/steamdeck/deck campaign run \
  <bundle-dir> candidate <same-run-id> <same-results-dir>
```

Staging first rejects every tracked, untracked, or ignored worktree entry, then
rebuilds the wrapper image from the exact frozen SteamRT4 base digest. The
source is mounted read-only and the fresh cross-build stays outside it; the
build manifest records the resulting wrapper image ID.
`campaign run` executes `deck doctor` immediately before transfer. Its Deck
helper requires the exact listed bundle inventory in a fresh run-specific
remote directory, binds its digest into the results, records the governor and
environment, and pins every CPU once around all 600 calibration or 1,200
candidate invocations. Restoration errors or any before/after governor mismatch
fail the phase while retaining diagnostics and partial artifacts. Run IDs and
phase-output existence checks prevent a rerun from overwriting evidence;
retrieved phase checksums are verified locally. Use a new run ID and results
directory for every rerun. Do not edit the bundle between device legs.

The retained evidence bundle contains sanitized raw calibration and candidate
JSON, threshold manifests, per-device reports, the cross-device decision,
build manifests, the exact binary/config/tool/benchmark-source bundle,
sanitized compile and link commands, every invocation's stderr and context,
governor/environment/phase logs, collection commands, and `SHA256SUMS`. It also
retains command, exit-status, and exact-tree identity evidence for the full
normal suite, the deterministic 1,000-run flush gate, ASan/UBSan, TSan,
warnings-as-errors, archive/rescan/token/dirty-owner/generation checks, and the
warmed zero-allocation checks. Hostnames, local paths, account data, and
private infrastructure are excluded. Any rerun records the invalidation reason
and preserves the superseded raw artifact. The optimization-log fragment
records the bundle location and digest, the result, limits, and
reconsideration condition.

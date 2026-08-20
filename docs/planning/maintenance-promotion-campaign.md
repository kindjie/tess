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

The measured implementation is frozen by a build manifest that binds the clean
source commit, raw config and canonical config, benchmark and campaign-tool
sources, benchmark binary, compiler, sanitized compile and link commands, and
the pinned SDK or native build context. The binary also embeds the source,
config, benchmark-source, and tool digests; manifest creation and every
observation verify them from Google Benchmark context. A change to the adapter,
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

Configure and build the exact committed M3 harness. The native leg uses the
same `build-manifest` command as the Deck staging route; it rejects a dirty
tracked tree, a mismatched source SHA, incomplete compile/link provenance, or a
binary whose embedded identities differ:

```sh
cmake --preset bench-only
cmake --build build/bench-only --target tess_bench_maintenance_campaign
```

Run `calibrate`, then `thresholds`. `collect` requires that exact valid
threshold manifest and embeds both its digest and the calibration digest;
`analyze` rejects another calibration. Pass the device-specific values frozen
in `bench/maintenance-campaign.json`; deviations fail closed. After both
reports exist, run `decide` with exactly those two reports and the frozen
config.

The Deck route is deliberately separate from the generic one-binary benchmark
helper. Stage and hash the complete cross-built bundle without contacting the
device, then run one whole pinned phase at a time:

```sh
tools/steamdeck/deck campaign stage <empty-bundle-dir>
tools/steamdeck/deck campaign run <bundle-dir> calibration <results-dir>
tools/steamdeck/deck campaign run <bundle-dir> candidate <results-dir>
```

`campaign run` executes `deck doctor` immediately before transfer. Its Deck
helper verifies the bundle, records the governor and environment, pins every
CPU once around all 600 calibration or 1,200 candidate invocations, restores
the original per-CPU governors on every exit, and retrieves partial artifacts
even after failure. Do not edit the bundle between device legs.

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

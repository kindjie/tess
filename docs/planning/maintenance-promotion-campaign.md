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

The measured implementation is frozen by the source commit, benchmark binary
SHA-256, campaign-tool SHA-256, and the compiler executable SHA-256 and version
recorded in every raw file. A change to the adapter, registered scheduler,
backend, benchmark, fixture, compiler, or effective flags invalidates both
timing legs. A later
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
and per-process peak RSS. Allocation authority comes from the separate warmed
dense and sparse adapter tests under every backend so allocator instrumentation
does not distort timing.

Each device uses 30 blocks and a 50 ms minimum measurement per isolated cell.
Within a block the collector applies an exact seeded shuffle and records each
invocation's order. CPU time is the decision metric; wall time, work counters,
memory, environment, source and binary identities, and raw Google Benchmark
context remain explanatory evidence. Campaigns serialize on each physical
device. Absolute timings are never compared between devices.

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
amplification sanity control. Dirty bit promotes only when the primary result
materially wins on at least one device and neither device has a material
guardrail regression. Otherwise it remains experimental, with flat,
regressed, or inconclusive evidence recorded explicitly.

## Reproduction and retention

Configure and build the exact committed harness before substituting its full
source SHA in the commands:

```sh
cmake --preset bench-only
cmake --build build/bench-only --target tess_bench_maintenance_campaign
```

In the same build environment, run `tools/maintenance_campaign.py toolchain`
with the compiler from the benchmark's compile command. It records the
compiler executable digest and version in a portable manifest; both device
phases consume that manifest, so a cross-built Deck campaign does not
substitute a compiler from SteamOS. Run `calibrate`, then `thresholds`. Run
`collect` only when the threshold manifest is valid, followed by `analyze`.
Pass the device-specific values frozen in `bench/maintenance-campaign.json`;
deviations fail closed. After both reports exist, run `decide` with exactly
those two reports and the frozen config.

The retained evidence bundle contains sanitized raw calibration and candidate
JSON, threshold manifests, per-device reports, the cross-device decision,
toolchain manifests, sanitized compile commands, collection commands, and
`SHA256SUMS`. Hostnames, local paths, account data, and private infrastructure
are excluded. The optimization-log fragment records the bundle location and
digest, the result, limits, and reconsideration condition.

# v0.13 maintenance promotion evidence

This directory retains the MNT-3 correctness and paired performance evidence
for source commit `b4a882bbdaa32a704109d5bdd773a1adfe45b492`. The frozen
campaign protocol is
[`maintenance-promotion-campaign.md`](../../../maintenance-promotion-campaign.md).

## Result and authority

The exact-commit correctness gate passed. The normal suite completed 1,568
tests with 1,567 passed, one intentionally unsupported capability skipped,
and no failures. Adapter-focused ASan/UBSan, TSan, and warnings-as-errors runs
passed 21 of 21 cases each; the campaign-tool suites passed 43 of 43 cases.

The M3 device report is `flat`. The Steam Deck primary comparison against the
queued-coalescing control is also `flat`, but its overall decision is
`material_regression`: immediate-execution guardrails regress materially in
the budgeted, flush, scaling-256, and scaling-1,024 cells. The scaling-4,096
immediate interval is `inconclusive`. The exact cross-device decision is
therefore `keep_experimental`.

This evidence keeps `DirtyBitScheduler` experimental. It does not veto
promotion of the separately gated stable task, handle, result, adapter, and
immediate-execution contract.

The result may be reconsidered only after a relevant implementation, adapter,
benchmark, fixture, compiler, effective-flag, or SDK change, with a newly
frozen candidate and complete correctness, M3, and Steam Deck legs. A purely
mechanical move may carry the evidence forward only with the explicit
representativeness record required by the execution plan. No post-result
threshold was introduced for the isolated scaling-4,096 peak-RSS observation;
the retained record treats it as descriptive process-RSS noise rather than a
promotion gate.

## Public sanitized bundle

`maintenance-campaign-b4a882bb-evidence-public.tar.gz` is the public
derivative of the campaign evidence. It contains provenance bindings,
measured binaries, and manifests — not full source, compiler, or build
trees, which are identified by digest and recorded identity only:

- build manifests binding the exact source commit, benchmark binary,
  compiler identity, sanitized compile and link commands, resolved
  compile and link drivers, native build context, and the pinned Steam
  Runtime image;
- the frozen campaign config, collector/analyzer tool, campaign benchmark
  source file, native and Deck runner scripts, and the measured binaries
  each leg executed;
- raw 30-block A/A calibration and candidate observations, thresholds,
  per-device reports, replay outputs, and the cross-device decision;
- environment, work-counter, peak-RSS, stderr, context, order,
  phase-status, governor, and exact-inventory manifests;
- exact-commit correctness, sanitizer, warnings-as-errors, allocation, and
  tooling logs and statuses; and
- failed-attempt invalidation records plus the sanitized Deck execution
  and retrieval histories.

The bundle is compressed because the raw JSON observations exceed the
repository's per-text-file token limit. Compression cannot bypass the
repository's public-safety rules:
`tests/test_maintenance_evidence_archive.py` re-derives the exact
inventory, rejects unsafe member metadata and unbounded sizes, and applies
the repository's public-safety patterns to every extracted member.

Verify on macOS or another system with `shasum`:

```sh
cd docs/planning/evidence/v0.13/maintenance
shasum -a 256 -c SHA256SUMS
mkdir maintenance-campaign-evidence
tar -xzf maintenance-campaign-b4a882bb-evidence-public.tar.gz \
  -C maintenance-campaign-evidence
cd maintenance-campaign-evidence
shasum -a 256 -c PUBLIC_EVIDENCE_SHA256SUMS
```

## External operational artifacts

The public bundle is sanitized; the exact operational evidence stays
external and unchanged. [`REDACTION-MAP.md`](REDACTION-MAP.md) records the
distinction precisely: one member is a labeled sanitized transcription and
two raw privilege bodies are omitted, each pinned to its raw SHA-256. The
raw set's frozen inner manifest `CROSS_DEVICE_EVIDENCE_V4_SHA256SUMS`
(SHA-256
`407f6279aad3a27442ca4fb8673712baf1b7c4a152c20e74607ff0a10cd77cb0`,
117 entries) is retained byte-identically inside the public bundle, so
every member other than the three dispositions still verifies against the
raw manifest.

The Deck console wrapper's aggregate console transcript and aggregate process
status were not separately retained. This is not claimed otherwise. The
authoritative phase records independently retain status 0 for calibration and
candidate, exact phase inventories, empty stderr, matching before/during/after
governor state, and replayable raw results. The sanitized execution history
also records cleanup and the superseded failed attempts.

## Representativeness of post-freeze changes

Every repository change after the frozen source `b4a882bb` is evidence
tooling, records, tests, or documentation: this evidence directory, the
redaction map and its archive regression test, the planning and roadmap
records, and a configure-time fix that resolves the campaign source
identity through `cmake/TessMaintenanceCampaignSourceSha.cmake` instead
of an unconditional `git rev-parse` (benchmark configuration previously
failed without Git or a `.git` directory). None of it changes the
measured workloads, the maintenance implementation or adapter, the
campaign benchmark source, fixtures, effective compile or link flags, the
SDK, or the evidence-admissibility semantics. In a Git checkout the
resolver embeds the same commit identity the frozen build embedded, and a
sentinel-carrying binary is rejected by evidence staging. The measured
binaries in the bundle therefore remain representative of the frozen
candidate, and the recorded result stands without a rerun.

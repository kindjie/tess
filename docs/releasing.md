# Release process

## Exact-SHA release mode

Dispatch `.github/workflows/ci.yml` with all three inputs:

- `ref`: the branch or tag being evaluated;
- `expected_version`: the complete version, such as `1.0.0-rc.1`; and
- `expected_sha`: the full commit SHA.

Supplying none runs the ordinary manual full tier. Supplying only part of the
release tuple fails. Release mode checks out and verifies the requested
identity, bypasses path filtering, and requires the complete platform,
configuration, sanitizer, no-exception, no-RTTI, documentation, package,
fuzzing, compatibility, and toolchain-floor matrix. The final job fails if any
required job failed or was skipped and uploads `release-evidence-<sha>` with
the tested SHA, expected version, tool versions, snapshot inventory, and gate
results.

## 0.13 and the 1.0 candidate

Publish `v0.13.0` only after all breaking API changes and the
[upgrade guide](upgrade-1.0.md) have landed. Before `v1.0.0-rc.1`:

1. add the immutable `compatibility/1.0.0-rc.1` snapshot;
2. assemble changelog fragments for the candidate;
3. solicit and record one substantial downstream evaluation without naming a
   private consumer; and
4. run release mode against the exact candidate SHA and version.

An RC release-mode fuzz job runs the archive target for 60 minutes with
ASan/UBSan, satisfying the cumulative final-SHA minimum in one reproducible
campaign. Commit every minimized regression input found by that or a shorter
scheduled campaign.

Tag the candidate only after that exact-SHA run passes. Observe it for at least
seven days, then rerun release mode on the same SHA. Any code change requires a
new RC and restarts the observation window. A documentation-only correction
does not restart the time window, but its new GA commit must still pass release
mode.

## 1.0 general availability

For GA, clear `TESS_VERSION_PRERELEASE`, assemble all three fragment streams,
and run the exact GA commit through release mode. Before tagging, verify there
is no open, untriaged correctness, security, performance-gate, or
release-process incident. Preserve the successful evidence artifact with the
release records, then tag `v1.0.0`.

The vcpkg overlay remains checkout-based through GA. Publish a central-registry
recipe and its release-archive hash only after the archive exists; neither is a
self-fetching in-tree release gate.

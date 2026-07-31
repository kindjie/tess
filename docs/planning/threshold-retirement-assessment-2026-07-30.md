# Threshold-gate retirement: criteria assessment, 2026-07-30

Redesign phase 4 retires the `tess_bench_*_thresholds` gating targets
only when the [section 4.3 exit criteria](test-and-benchmark-redesign.md)
hold, and section 10 is explicit that failing them is a result rather
than a blockage: *"If the criteria are not met, this phase does not run
and the ceilings stay; that outcome is a legitimate result, not a
blocked phase."*

This is that write-up. **The criteria are not met. The ceilings stay.**

## Criterion 1 — shadow period covering a full release cycle

**Not met.** The replacements entered shadow mode on 2026-07-29: the
counter goldens with the probe and checker, the paired sentinel run as
a path-filtered pull-request job. The most recent release, v0.4.0,
predates that (2026-07-20), so no release cycle has elapsed *since*
shadow mode began. The measured shadow record is about one day long
and contains no release boundary.

This is the binding criterion. It cannot be advanced by work, only by
elapsed time and a release.

## Criterion 2 — replay reproduces the 2026-07-23 detection

**Met locally; hosted confirmation dispatched.** Replaying the paired
run over the exact base/head pair that CI run 30041607406 measured
(`c300560` to `f92b5f5`) reproduced the detection on all four
confirmed-catch sentinels, at +395%, +158%, +156% and +9.3%
(optimization log, "Paired Sentinel Replay Reproduces The 2026-07-23
Catch"). Section 4.1's note that the hosted replay remains a phase 4
criterion is the reason a hosted dispatch was run on 2026-07-30; its
verdict belongs in this file when it lands.

## Criterion 3 — no unexplained divergence

**Cannot yet be evaluated.** A divergence record needs a shadow period
long enough to contain ceiling firings. The shadow window so far
contains none, so the criterion is neither satisfied nor violated —
there is simply no evidence either way, which is not the same as
passing.

One divergence is already on the record from *before* shadow mode and
is explained: the change-point backtest found a sustained 3-4x
fields-family step at the v0.12 merge (`61f8044`) that the ceilings
did not fire on, because recalibration absorbed it. That is a ceiling
false negative rather than a replacement failure, and it argues for
the replacement rather than against it — but it is a standing suspect
awaiting a maintainer intent-versus-regression decision, not a
resolved data point.

## Criterion 4 — false-positive rate at or below the ceilings'

**Cannot yet be evaluated.** The ceilings' own rate is five false
firings in six (section 2.3). The replacement has not accumulated
enough firings in shadow mode to estimate a rate at all. Comparing a
zero-sample rate against a six-sample one would be arithmetic, not
evidence.

## What would change this assessment

In order of what gates what:

1. A release cut after 2026-07-29, so criterion 1's clock can complete.
2. Shadow-mode firings from both the ceilings and the replacements over
   that period, which is what criteria 3 and 4 measure.
3. The hosted replay verdict recorded above.

Until then the ceilings remain authoritative, the replacements remain
advisory, and `TESS_COUNTER_GOLDENS_STRICT=1` remains the unflipped
promotion switch.

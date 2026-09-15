# v1.0.0 release preparation and supplemental evidence

## RC.1 CMake floor download failure

The post-observation [RC.1 run][rc-run] targets unchanged source commit
`566234100187769fe4d7b083448f5e844a0c1f85`. Its CMake 3.25.3 floor job
failed before configuration because the pinned cmake.org URL returned
HTTP 403. The same failure reproduced outside CI.

Kitware's [official GitHub release][cmake-release] provides the same
`cmake-3.25.3-linux-x86_64.tar.gz` archive. Its SHA-256 matches the original
lock exactly:

```text
d4d2ba83301b215857d3b6590cd4434a414fa151c5807693abe587bd6c03581e
```

The supplemental check used a `git archive` of that exact RC commit and the
verified binary inside a network-disabled Linux x86-64 container. The GCC
image digest was
`sha256:3ae7320d7dd41f446a48930e1edf4e7a41c7be5ae43a5ddbf017c53fe6495738`;
the tools reported CMake 3.25.3 and GCC 14.4.0. Configuration used Release,
with tests and examples disabled, followed by the RC's unchanged
`tools/install_smoke.sh`. Configure, install, package discovery, build, and
all three installed consumers passed. This is supplemental local Linux
container evidence, not a claim that the hosted CMake job passed.

The maintainer accepts this substitution only for the unavailable CMake
floor download in the RC rerun. Every other required RC check must pass,
and the complete exact-GA release run must pass with the corrected download
URL. Neither the RC tag nor its source is modified. Preserve the failed-job
log and successful supplemental log with the release evidence; retry the
hosted failed job before relying on the substitution.

## Remaining publication evidence

The RC run is still in progress at preparation time. The exact GA SHA and
release-mode run are assigned after the preparation merges. No successful
GA run or published tag is claimed by this preparation record. The
[release process](../../../releasing.md) owns the final checks.

[rc-run]: https://github.com/kindjie/tess/actions/runs/34917981800
[cmake-release]: https://github.com/Kitware/CMake/releases/tag/v3.25.3
